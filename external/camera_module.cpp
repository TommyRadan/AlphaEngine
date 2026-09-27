/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file camera_module.cpp
 * @brief The player's free-flying camera: a @c fly_camera behaviour on a node
 *        carrying a perspective @c camera_component.
 *
 * Controls: WASD to move, space / ctrl to rise / sink, shift to move faster,
 * hold the left mouse button and drag to look around.
 */

#include "api/game_module.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>

#include <core/event_engine.hpp>
#include <core/math/math.hpp>
#include <core/settings.hpp>
#include <core/subscription.hpp>
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

    // The keys the camera polls every frame. Their state lives in a table of
    // this fixed size, indexed by position in this list, rather than in a map
    // keyed by key_code: tracking a key never allocates, and a key that is not
    // listed here is dropped on the way in instead of accumulating an entry.
    // Both sides of each modifier are tracked so the right-hand shift / ctrl
    // work the same as the left.
    constexpr std::array<core::key_code, 9> tracked_keys{core::key_code::w,
                                                         core::key_code::a,
                                                         core::key_code::s,
                                                         core::key_code::d,
                                                         core::key_code::space,
                                                         core::key_code::left_ctrl,
                                                         core::key_code::right_ctrl,
                                                         core::key_code::left_shift,
                                                         core::key_code::right_shift};

    // Position of @p code in tracked_keys, or tracked_keys.size() if it is not tracked.
    std::size_t key_slot(core::key_code code)
    {
        for (std::size_t i = 0; i < tracked_keys.size(); ++i)
        {
            if (tracked_keys[i] == code)
            {
                return i;
            }
        }
        return tracked_keys.size();
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
     * Flies its node — the camera's pose — from keyboard and mouse input.
     *
     * The node's transform is the camera's pose (its camera_component views
     * from the node), read fresh every frame, so anything else that places the
     * node (a demo framing its model) simply hands over to the controls. While
     * enabled it listens to the input events and keeps the held keys and the
     * look button in members; movement is applied per rendered frame, so it
     * stays smooth at the render rate, and mouse-look per motion event. The
     * node moves in its parent's frame, which is world space under the scene
     * root.
     */
    struct fly_camera final : runtime::behavior
    {
        void on_enable() override
        {
            core::event_bus& events = *runtime::current_engine().events;
            m_key_down = events.subscribe<core::key_down>([this](const core::key_down& event)
                                                          { set_key(event.m_key_code, true); });
            m_key_up =
                events.subscribe<core::key_up>([this](const core::key_up& event) { set_key(event.m_key_code, false); });
            m_mouse_key_down = events.subscribe<core::mouse_key_down>(
                [this](const core::mouse_key_down& event)
                {
                    if (event.m_key_code == core::mouse_key_code::left)
                    {
                        m_look_button_held = true;
                    }
                });
            m_mouse_key_up = events.subscribe<core::mouse_key_up>(
                [this](const core::mouse_key_up& event)
                {
                    if (event.m_key_code == core::mouse_key_code::left)
                    {
                        m_look_button_held = false;
                    }
                });
            m_mouse_move = events.subscribe<core::mouse_move>([this](const core::mouse_move& event) { look(event); });
        }

        void on_disable() override
        {
            m_key_down.reset();
            m_key_up.reset();
            m_mouse_key_down.reset();
            m_mouse_key_up.reset();
            m_mouse_move.reset();
            // The releases are not heard while disabled, so let go of
            // everything rather than come back with a key stuck down.
            m_keys.fill(false);
            m_look_button_held = false;
        }

        void on_update(float delta_time) override
        {
            rendering_engine::util::transform& pose = owner().transform;
            const core::math::vec3 position = pose.get_position();
            const core::math::vec3 forward = pose.get_forward();

            // The forward is unit length; the right is zero only while looking
            // straight along the up axis, which the mouse-look pitch clamp
            // prevents.
            const core::math::vec3 right = safe_normalize(core::math::cross(forward, up_vector));

            float speed = 3.0f;
            if (is_key_down(core::key_code::left_shift) || is_key_down(core::key_code::right_shift))
            {
                speed = 30.0f;
            }

            const float distance = speed * (delta_time / 1000.0f);
            core::math::vec3 new_position = position;

            if (is_key_down(core::key_code::w))
            {
                new_position += forward * distance;
            }

            if (is_key_down(core::key_code::a))
            {
                new_position -= right * distance;
            }

            if (is_key_down(core::key_code::s))
            {
                new_position -= forward * distance;
            }

            if (is_key_down(core::key_code::d))
            {
                new_position += right * distance;
            }

            if (is_key_down(core::key_code::space))
            {
                new_position += up_vector * distance;
            }

            if (is_key_down(core::key_code::left_ctrl) || is_key_down(core::key_code::right_ctrl))
            {
                new_position -= up_vector * distance;
            }

            pose.set_position(new_position);
        }

    private:
        void set_key(core::key_code code, bool down)
        {
            const std::size_t slot = key_slot(code);
            if (slot < tracked_keys.size())
            {
                m_keys[slot] = down;
            }
        }

        bool is_key_down(core::key_code code) const
        {
            const std::size_t slot = key_slot(code);
            return slot < tracked_keys.size() && m_keys[slot];
        }

        void look(const core::mouse_move& event)
        {
            // Mouse-look only while the left button is held, in every build
            // configuration. The debug overlay already withholds mouse events
            // while it is capturing the pointer, so it needs no special case
            // here.
            if (!m_look_button_held)
            {
                return;
            }

            rendering_engine::util::transform& pose = owner().transform;
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
            const float yaw_angle = -event.m_delta_x * sensitivity;
            const float pitch_angle = pitch_direction * event.m_delta_y * sensitivity;

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

        std::array<bool, tracked_keys.size()> m_keys{};
        bool m_look_button_held{false};

        // Input listeners, held only while the camera is enabled.
        core::subscription m_key_down;
        core::subscription m_key_up;
        core::subscription m_mouse_key_down;
        core::subscription m_mouse_key_up;
        core::subscription m_mouse_move;
    };
} // namespace

GAME_MODULE()
{
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
