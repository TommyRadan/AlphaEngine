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

#include <map>

#include "api/camera.hpp"
#include "api/game_module.hpp"
#include "api/log.hpp"
#include "api/time.hpp"

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <rendering_engine/rendering_engine.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/vector_angle.hpp>

camera_id g_camera_id = 0;
std::map<event_engine::key_code, bool> keys;
std::map<event_engine::mouse_key_code, bool> mouse_keys;

static void on_engine_start(const event_engine::event& event)
{
    g_camera_id = create_camera(camera_type::perspective);
    attach_camera(g_camera_id);

    set_camera_pos(g_camera_id, -5.0, 0.0, 0.0);
}

static void on_engine_stop(const event_engine::event& event)
{
    destroy_camera(g_camera_id);
}

static void on_key_down(const event_engine::event& event)
{
    auto key_event = dynamic_cast<const event_engine::key_down*>(&event);
    keys[key_event->m_key_code] = true;
}

static void on_key_up(const event_engine::event& event)
{
    auto key_event = dynamic_cast<const event_engine::key_up*>(&event);
    keys[key_event->m_key_code] = false;
}

static void on_mouse_key_down(const event_engine::event& event)
{
    auto mouse_event = dynamic_cast<const event_engine::mouse_key_down*>(&event);
    mouse_keys[mouse_event->m_key_code] = true;
}

static void on_mouse_key_up(const event_engine::event& event)
{
    auto mouse_event = dynamic_cast<const event_engine::mouse_key_up*>(&event);
    mouse_keys[mouse_event->m_key_code] = false;
}

static void on_frame(const event_engine::event& event)
{
    if (g_camera_id == 0)
    {
        return;
    }

    glm::vec3 position;
    get_camera_pos(g_camera_id, &position.x, &position.y, &position.z);

    glm::vec3 rotation;
    get_camera_rot(g_camera_id, &rotation.x, &rotation.y, &rotation.z);

    float speed = 3.0f;
    if (keys[event_engine::key_code::shift])
    {
        speed = 30.0f;
    }

    float distance = speed * ((float)get_delta_time() / 1000);
    glm::vec3 up_vector{0.0f, 0.0f, 1.0f};
    glm::vec3 new_position = position;

    if (keys[event_engine::key_code::w])
    {
        new_position += rotation * distance;
    }

    if (keys[event_engine::key_code::a])
    {
        new_position -= glm::cross(rotation, up_vector) * distance;
    }

    if (keys[event_engine::key_code::s])
    {
        new_position -= rotation * distance;
    }

    if (keys[event_engine::key_code::d])
    {
        new_position += glm::cross(rotation, up_vector) * distance;
    }

    if (keys[event_engine::key_code::space])
    {
        new_position += up_vector * distance;
    }

    if (keys[event_engine::key_code::ctrl])
    {
        new_position -= up_vector * distance;
    }

    set_camera_pos(g_camera_id, new_position.x, new_position.y, new_position.z);
}

static void on_mouse_move(const event_engine::event& event)
{
    auto move_event = dynamic_cast<const event_engine::mouse_move*>(&event);
    int32_t delta_x = move_event->m_x;
    int32_t delta_y = move_event->m_y;

#if _DEBUG
    if (!mouse_keys[event_engine::mouse_key_code::left])
    {
        return;
    }
#endif

    if (g_camera_id == 0)
    {
        return;
    }

    glm::vec3 rotation;
    get_camera_rot(g_camera_id, &rotation.x, &rotation.y, &rotation.z);
    glm::vec3 up_vector{0.0f, 0.0f, 1.0f};

    // Normalize the rotation vector to get the forward direction
    glm::vec3 forward = glm::normalize(rotation);

    // Calculate the right vector (perpendicular to forward and up)
    glm::vec3 right = glm::normalize(glm::cross(forward, up_vector));

    const auto& s = *control::current_engine().settings;
    float sensitivity = s.get_mouse_sensitivity();
    int pitch_multiplier = s.is_mouse_reversed() ? -1 : 1;

    // Calculate rotation angles in radians
    float yaw_angle = delta_x * sensitivity;
    float pitch_angle = delta_y * sensitivity * pitch_multiplier;

    // Create rotation matrices
    glm::mat4 yaw_rotation = glm::rotate(yaw_angle, up_vector);
    glm::mat4 pitch_rotation = glm::rotate(pitch_angle, right);

    // Apply rotations
    glm::vec4 rotated = glm::vec4(forward, 0.0f) * yaw_rotation * pitch_rotation;
    glm::vec3 new_rotation = glm::vec3(rotated.x, rotated.y, rotated.z);

    // Prevent looking too far up or down
    if (glm::abs(new_rotation.z) > 0.99f)
    {
        new_rotation = forward;
    }

    set_camera_rot(g_camera_id, new_rotation.x, new_rotation.y, new_rotation.z);
}

GAME_MODULE()
{
    LOG_INF("Registering external module: camera_module");
    struct game_module_info info = {};
    info.on_frame = on_frame;
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
