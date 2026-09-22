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

#include <array>
#include <cmath>
#include <cstddef>

#include "api/camera.hpp"
#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <core/settings.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <runtime/engine.hpp>

namespace
{
    // The engine's up axis: the camera yaws about it and pitch is measured against it.
    constexpr core::math::vec3 up_vector{0.0f, 0.0f, 1.0f};

    // A forward vector whose |z| exceeds this is too close to the vertical
    // for a stable right axis, so mouse-look refuses to pitch past it.
    constexpr float pitch_limit = 0.99f;

    // The keys this module polls every frame. Their state lives in a table of
    // this fixed size, indexed by position in this list, rather than in a map
    // keyed by key_code: tracking a key never allocates, and a key that is not
    // listed here is dropped on the way in instead of accumulating an entry.
    constexpr std::array<core::key_code, 7> tracked_keys{core::key_code::w,
                                                         core::key_code::a,
                                                         core::key_code::s,
                                                         core::key_code::d,
                                                         core::key_code::space,
                                                         core::key_code::ctrl,
                                                         core::key_code::shift};

    camera_id g_camera_id = invalid_camera_id;
    std::array<bool, tracked_keys.size()> g_keys{};
    bool g_look_button_held = false;

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

    void set_key(core::key_code code, bool down)
    {
        const std::size_t slot = key_slot(code);
        if (slot < tracked_keys.size())
        {
            g_keys[slot] = down;
        }
    }

    bool is_key_down(core::key_code code)
    {
        const std::size_t slot = key_slot(code);
        return slot < tracked_keys.size() && g_keys[slot];
    }

    // Unit vector along @p v, or the zero vector when @p v has no direction.
    core::math::vec3 safe_normalize(const core::math::vec3& v)
    {
        const float len = core::math::length(v);
        return len > 0.0f ? v / len : core::math::vec3{};
    }
} // namespace

static void on_engine_start(const core::engine_start& event)
{
    g_camera_id = create_camera(camera_type::perspective);
    attach_camera(g_camera_id);

    set_camera_pos(g_camera_id, -5.0f, 0.0f, 0.0f);
    // Give the camera a sensible initial forward (+X, toward the origin
    // where the demos place their geometry). Without it the forward is the
    // zero vector and the view matrix is degenerate, so nothing is visible
    // until the first mouse move.
    set_camera_rot(g_camera_id, 1.0f, 0.0f, 0.0f);
}

static void on_engine_stop(const core::engine_stop& event)
{
    destroy_camera(g_camera_id);
    g_camera_id = invalid_camera_id;
}

static void on_key_down(const core::key_down& event)
{
    set_key(event.m_key_code, true);
}

static void on_key_up(const core::key_up& event)
{
    set_key(event.m_key_code, false);
}

static void on_mouse_key_down(const core::mouse_key_down& event)
{
    if (event.m_key_code == core::mouse_key_code::left)
    {
        g_look_button_held = true;
    }
}

static void on_mouse_key_up(const core::mouse_key_up& event)
{
    if (event.m_key_code == core::mouse_key_code::left)
    {
        g_look_button_held = false;
    }
}

static void on_render_update(const core::render_update& event)
{
    if (g_camera_id == invalid_camera_id)
    {
        return;
    }

    core::math::vec3 position;
    core::math::vec3 rotation;
    if (!get_camera_pos(g_camera_id, &position.x, &position.y, &position.z) ||
        !get_camera_rot(g_camera_id, &rotation.x, &rotation.y, &rotation.z))
    {
        return;
    }

    // The stored rotation is the forward direction but not necessarily unit
    // length; move along the unit vectors so the speed does not depend on it.
    const core::math::vec3 forward = safe_normalize(rotation);
    const core::math::vec3 right = safe_normalize(core::math::cross(forward, up_vector));

    float speed = 3.0f;
    if (is_key_down(core::key_code::shift))
    {
        speed = 30.0f;
    }

    const float distance = speed * (event.m_delta_time / 1000.0f);
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

    if (is_key_down(core::key_code::ctrl))
    {
        new_position -= up_vector * distance;
    }

    set_camera_pos(g_camera_id, new_position.x, new_position.y, new_position.z);
}

static void on_mouse_move(const core::mouse_move& event)
{
    // Mouse-look only while the left button is held, in every build
    // configuration. Release builds used to look freely while only debug
    // builds required the button, so the two configurations handled the
    // mouse differently; the debug overlay already withholds mouse events
    // while it is capturing the pointer, so it needs no special case here.
    if (!g_look_button_held)
    {
        return;
    }

    if (g_camera_id == invalid_camera_id)
    {
        return;
    }

    core::math::vec3 rotation;
    if (!get_camera_rot(g_camera_id, &rotation.x, &rotation.y, &rotation.z))
    {
        return;
    }

    const core::math::vec3 forward = safe_normalize(rotation);
    const core::math::vec3 right = safe_normalize(core::math::cross(forward, up_vector));
    if (core::math::length(right) <= 0.0f)
    {
        // No forward, or a forward along the up axis: there is no frame to rotate in.
        return;
    }

    const auto& s = *runtime::current_engine().settings;
    const float sensitivity = s.input.mouse_sensitivity;
    const float pitch_direction = s.input.mouse_reversed ? 1.0f : -1.0f;

    // Angles in radians. Mouse right turns the view right, which in this
    // right-handed frame is a clockwise (negative) rotation about +Z; mouse
    // down (positive delta) pitches the view down, a negative rotation about
    // the right axis, unless the settings ask for a reversed pitch.
    const float yaw_angle = -static_cast<float>(event.m_x) * sensitivity;
    const float pitch_angle = pitch_direction * static_cast<float>(event.m_y) * sensitivity;

    const core::math::mat4 yaw_rotation = core::math::rotate(yaw_angle, up_vector);
    const core::math::mat4 pitch_rotation = core::math::rotate(pitch_angle, right);

    // Column-vector convention, matching core::math::rotate: the right-most
    // factor applies first. Pitching about the current right axis and then
    // yawing about the world up axis equals yawing first and then pitching
    // about the yawed right axis, so this composes as "yaw, then pitch".
    const core::math::vec4 yawed = yaw_rotation * core::math::vec4{forward, 0.0f};
    const core::math::vec4 pitched = yaw_rotation * pitch_rotation * core::math::vec4{forward, 0.0f};

    core::math::vec3 new_forward{pitched.x, pitched.y, pitched.z};

    // Clamp the pitch without discarding the yaw: past the vertical limit
    // keep the yawed-only vector. Yawing about +Z leaves z untouched, so that
    // vector is within the limit whenever the previous forward was.
    if (std::abs(new_forward.z) > pitch_limit)
    {
        new_forward = core::math::vec3{yawed.x, yawed.y, yawed.z};
    }

    set_camera_rot(g_camera_id, new_forward.x, new_forward.y, new_forward.z);
}

GAME_MODULE()
{
    LOG_INF("Registering external module: camera_module");
    struct game_module_info info = {};
    info.on_render_update = on_render_update;
    info.on_engine_start = on_engine_start;
    info.on_engine_stop = on_engine_stop;
    info.on_key_down = on_key_down;
    info.on_key_up = on_key_up;
    info.on_mouse_key_down = on_mouse_key_down;
    info.on_mouse_key_up = on_mouse_key_up;
    info.on_mouse_move = on_mouse_move;
    register_game_module(info);
    return true;
}
