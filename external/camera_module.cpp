// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file camera_module.cpp
 * @brief The player's free-flying camera: a @c fly_camera behaviour on a node
 *        carrying a perspective @c camera_component.
 *
 * Controls: WASD to move, space / ctrl to rise / sink, shift to move faster,
 * hold the left mouse button and drag to look around. On a connected gamepad:
 * the left stick moves, the right stick looks, the shoulder buttons rise /
 * sink and clicking the left stick sprints.
 */

#include "api/game_module.hpp"

#include <cmath>
#include <memory>

#include <core/input.hpp>
#include <core/math/math.hpp>
#include <core/settings.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/engine.hpp>

namespace
{
    // The engine's up axis (core/math/math.hpp): the camera yaws about it and
    // pitch is measured against it.
    constexpr core::math::vec3 up_vector = core::math::world_up;

    // A forward vector whose |z| exceeds this is too close to the vertical
    // for a stable right axis, so mouse-look refuses to pitch past it.
    constexpr float pitch_limit = 0.99f;

    // Where the camera starts: back along -X, facing +X toward the origin
    // where the demos place their geometry.
    constexpr core::math::vec3 start_position{-5.0f, 0.0f, 0.0f};

    // Per-second look rate a fully-deflected right stick drives, in the same
    // units as a mouse delta (points) so it composes with mouse-look through
    // one sensitivity setting (see fly_camera::look).
    constexpr float gamepad_look_speed = 600.0f;

    // Registers the camera's default bindings once, ahead of any fly_camera
    // instance. A matching entry under `input.bindings` in settings.json
    // (core::input_settings::bindings) replaces one of these lists entirely;
    // see core/input.hpp for the binding-string grammar.
    void bind_controls(core::input& input)
    {
        using core::action_binding;
        using core::axis_binding;
        using core::axis_sign;
        using core::gamepad_axis_code;
        using core::gamepad_button_code;
        using core::key_code;
        using core::mouse_key_code;

        input.bind_action("move_forward",
                          {action_binding::from_key(key_code::w),
                           action_binding::from_gamepad_axis(gamepad_axis_code::left_y, axis_sign::negative)});
        input.bind_action("move_back",
                          {action_binding::from_key(key_code::s),
                           action_binding::from_gamepad_axis(gamepad_axis_code::left_y, axis_sign::positive)});
        input.bind_action("move_left",
                          {action_binding::from_key(key_code::a),
                           action_binding::from_gamepad_axis(gamepad_axis_code::left_x, axis_sign::negative)});
        input.bind_action("move_right",
                          {action_binding::from_key(key_code::d),
                           action_binding::from_gamepad_axis(gamepad_axis_code::left_x, axis_sign::positive)});
        input.bind_action("move_up",
                          {action_binding::from_key(key_code::space),
                           action_binding::from_gamepad_button(gamepad_button_code::right_shoulder)});
        input.bind_action("move_down",
                          {action_binding::from_key(key_code::left_ctrl),
                           action_binding::from_key(key_code::right_ctrl),
                           action_binding::from_gamepad_button(gamepad_button_code::left_shoulder)});
        input.bind_action("sprint",
                          {action_binding::from_key(key_code::left_shift),
                           action_binding::from_key(key_code::right_shift),
                           action_binding::from_gamepad_button(gamepad_button_code::left_stick)});
        input.bind_action("look_enable", {action_binding::from_mouse_button(mouse_key_code::left)});

        input.bind_axis("look_x", {axis_binding::from_gamepad_axis(gamepad_axis_code::right_x, 0.2f)});
        input.bind_axis("look_y", {axis_binding::from_gamepad_axis(gamepad_axis_code::right_y, 0.2f)});
    }

    // Unit vector along @p v, or the zero vector when @p v has no direction.
    core::math::vec3 safe_normalize(const core::math::vec3& v)
    {
        const float len = core::math::length(v);
        return len > 0.0f ? v / len : core::math::vec3{};
    }

    // A perspective camera built from the engine's configuration: the
    // settings' field of view and the drawable's aspect as the renderer last
    // reported it to the camera registry (the settings' logical size stands
    // in before the renderer is up). The registry keeps the aspect current
    // once the camera attaches.
    std::unique_ptr<rendering_engine::camera> make_perspective_camera()
    {
        const core::settings& s = *runtime::current_engine().settings;
        const float reported = rendering_engine::drawable_aspect();
        const float aspect_ratio = reported > 0.0f ? reported : s.window.aspect_ratio();
        return std::make_unique<rendering_engine::perspective_camera>(s.camera.field_of_view, aspect_ratio);
    }

    /**
     * Flies its node — the camera's pose — from the "move_*", "sprint" and
     * "look_enable" actions and the "look_x" / "look_y" axes @ref bind_controls
     * registers, instead of keeping its own key table and subscribing to raw
     * input events.
     *
     * The node's transform is the camera's pose (its camera_component views
     * from the node), read fresh every frame, so anything else that places the
     * node (a demo framing its model) simply hands over to the controls.
     * Movement and mouse-look are both applied per rendered frame, so they
     * stay smooth at the render rate; @c core::input keeps the state polled
     * here live rather than latched to the fixed step (see core/input.hpp),
     * which is what keeps the mouse-look feel unchanged from when it ran off
     * a per-event subscription. The node moves in its parent's frame, which
     * is world space under the scene root.
     */
    struct fly_camera final : runtime::behavior
    {
        void on_update(float delta_time) override
        {
            core::input& input = *runtime::current_engine().input;

            core::transform& pose = owner().transform;
            const core::math::vec3 position = pose.get_position();
            const core::math::vec3 forward = pose.get_forward();

            // The forward is unit length; the right is zero only while looking
            // straight along the up axis, which the mouse-look pitch clamp
            // prevents.
            const core::math::vec3 right = safe_normalize(core::math::cross(forward, up_vector));

            const float speed = input.is_action_down("sprint") ? 30.0f : 3.0f;
            const float distance = speed * (delta_time / 1000.0f);
            core::math::vec3 new_position = position;

            if (input.is_action_down("move_forward"))
            {
                new_position += forward * distance;
            }
            if (input.is_action_down("move_left"))
            {
                new_position -= right * distance;
            }
            if (input.is_action_down("move_back"))
            {
                new_position -= forward * distance;
            }
            if (input.is_action_down("move_right"))
            {
                new_position += right * distance;
            }
            if (input.is_action_down("move_up"))
            {
                new_position += up_vector * distance;
            }
            if (input.is_action_down("move_down"))
            {
                new_position -= up_vector * distance;
            }

            pose.set_position(new_position);

            look(input, delta_time);
        }

    private:
        void look(core::input& input, float delta_time)
        {
            // Mouse-look only while "look_enable" (the left mouse button by
            // default) is held, in every build configuration; the debug
            // overlay already withholds mouse input while it is capturing the
            // pointer, so is_action_down reflects that on its own. The right
            // stick looks unconditionally, the way a gamepad camera usually
            // works.
            core::math::vec2 delta{};
            if (input.is_action_down("look_enable"))
            {
                delta += input.mouse_delta();
            }
            const float stick_x = input.get_axis("look_x");
            const float stick_y = input.get_axis("look_y");
            if (stick_x != 0.0f || stick_y != 0.0f)
            {
                delta += core::math::vec2{stick_x, stick_y} * (gamepad_look_speed * (delta_time / 1000.0f));
            }
            if (delta.x == 0.0f && delta.y == 0.0f)
            {
                return;
            }

            core::transform& pose = owner().transform;
            const core::math::vec3 forward = pose.get_forward();

            const core::math::vec3 right = safe_normalize(core::math::cross(forward, up_vector));
            if (core::math::length(right) <= 0.0f)
            {
                // A forward along the up axis: there is no frame to rotate in.
                return;
            }

            const auto& s = *runtime::current_engine().settings;
            const float sensitivity = s.input.mouse_sensitivity;
            const float pitch_direction = s.input.mouse_reversed ? 1.0f : -1.0f;

            // Angles in radians. Mouse right turns the view right, which in
            // this right-handed frame is a clockwise (negative) rotation about
            // +Z; mouse down (positive delta) pitches the view down, a negative
            // rotation about the right axis, unless the settings ask for a
            // reversed pitch. The deltas keep their sub-pixel fraction, so a
            // slow drag still turns the camera instead of truncating to no
            // motion.
            const float yaw_angle = -delta.x * sensitivity;
            const float pitch_angle = pitch_direction * delta.y * sensitivity;

            const core::math::mat4 yaw_rotation = core::math::rotate(yaw_angle, up_vector);
            const core::math::mat4 pitch_rotation = core::math::rotate(pitch_angle, right);

            // Column-vector convention, matching core::math::rotate: the
            // right-most factor applies first. Pitching about the current right
            // axis and then yawing about the world up axis equals yawing first
            // and then pitching about the yawed right axis, so this composes as
            // "yaw, then pitch".
            const core::math::vec4 yawed = yaw_rotation * core::math::vec4{forward, 0.0f};
            const core::math::vec4 pitched = yaw_rotation * pitch_rotation * core::math::vec4{forward, 0.0f};

            core::math::vec3 new_forward{pitched.x, pitched.y, pitched.z};

            // Clamp the pitch without discarding the yaw: past the vertical
            // limit keep the yawed-only vector. Yawing about +Z leaves z
            // untouched, so that vector is within the limit whenever the
            // previous forward was.
            if (std::abs(new_forward.z) > pitch_limit)
            {
                new_forward = core::math::vec3{yawed.x, yawed.y, yawed.z};
            }

            // look_at stores the orientation as the transform's quaternion
            // (+Z up, with a horizontal fallback along the up axis).
            pose.look_at(pose.get_position() + new_forward);
        }
    };
} // namespace

REFLECT_TYPES()
{
    registry.register_behavior<fly_camera>("fly_camera");
}

GAME_MODULE()
{
    bind_controls(*runtime::current_engine().input);

    // The player's camera goes in the scene the engine hands the bootstrap —
    // the persistent one, so it outlives any level loaded later.
    runtime::node& camera = scene.create_node("fly_camera");
    camera.transform.set_position(start_position);
    // A fresh node already faces the engine forward (+X); this spells the
    // starting view out rather than relying on that default.
    camera.transform.look_at(start_position + core::math::world_forward);
    camera.add_component(runtime::camera_component{make_perspective_camera()});
    runtime::add_behavior<fly_camera>(camera);
}
