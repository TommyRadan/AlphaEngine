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

#include "API/GameModule.hpp"
#include "API/Time.hpp"
#include "API/Log.hpp"
#include "API/Camera.hpp"

#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/Settings.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/vector_angle.hpp>

CameraId cameraId = 0;
std::map<event_engine::key_code, bool> keys;
std::map<event_engine::mouse_key_code, bool> mouse_keys;

static void OnEngineStart(const event_engine::event& event)
{
    cameraId = CreateCamera(CameraType::Perspective);
    AttachCamera(cameraId);

    SetCameraPos(cameraId, -5.0, 0.0, 0.0);
}

static void OnEngineStop(const event_engine::event& event)
{
    DestroyCamera(cameraId);
}

static void OnKeyDown(const event_engine::event& event)
{
    auto key_event = dynamic_cast<const event_engine::key_down*>(&event);
    keys[key_event->m_key_code] = true;
}

static void OnKeyUp(const event_engine::event& event)
{
    auto key_event = dynamic_cast<const event_engine::key_up*>(&event);
    keys[key_event->m_key_code] = false;
}

static void OnMouseKeyDown(const event_engine::event& event)
{
    auto mouse_event = dynamic_cast<const event_engine::mouse_key_down*>(&event);
    mouse_keys[mouse_event->m_key_code] = true;
}

static void OnMouseKeyUp(const event_engine::event& event)
{
    auto mouse_event = dynamic_cast<const event_engine::mouse_key_up*>(&event);
    mouse_keys[mouse_event->m_key_code] = false;
}

static void OnFrame(const event_engine::event& event)
{
    if (cameraId == 0)
    {
        return;
    }

    glm::vec3 position;
    GetCameraPos(cameraId, &position.x, &position.y, &position.z);

    glm::vec3 rotation;
    GetCameraRot(cameraId, &rotation.x, &rotation.y, &rotation.z);

    float speed = 3.0f;
    if (keys[event_engine::key_code::SHIFT])
    {
        speed = 30.0f;
    }

    float distance = speed * ((float)GetDeltaTime() / 1000);
    glm::vec3 upVector {0.0f, 0.0f, 1.0f};
    glm::vec3 newPosition = position;

    if (keys[event_engine::key_code::W])
    {
        newPosition += rotation * distance;
    }

    if (keys[event_engine::key_code::A])
    {
        newPosition -= glm::cross(rotation, upVector) * distance;
    }

    if (keys[event_engine::key_code::S])
    {
        newPosition -= rotation * distance;
    }

    if (keys[event_engine::key_code::D])
    {
        newPosition += glm::cross(rotation, upVector) * distance;
    }

    if (keys[event_engine::key_code::SPACE])
    {
        newPosition += upVector * distance;
    }

    if (keys[event_engine::key_code::CTRL])
    {
        newPosition -= upVector * distance;
    }

    SetCameraPos(cameraId, newPosition.x, newPosition.y, newPosition.z);
}

static void OnMouseMove(const event_engine::event& event)
{
    auto move_event = dynamic_cast<const event_engine::mouse_move*>(&event);
    int32_t deltaX = move_event->m_x;
    int32_t deltaY = move_event->m_y;

#if _DEBUG
    if (!mouse_keys[event_engine::mouse_key_code::LEFT])
    {
        return;
    }
#endif

    if (cameraId == 0)
    {
        return;
    }

    glm::vec3 rotation;
    GetCameraRot(cameraId, &rotation.x, &rotation.y, &rotation.z);
    glm::vec3 rotationAxis { 0.0f, 0.0f, 1.0f };
    glm::vec3 upVector { 0.0f, 0.0f, 1.0f };
    glm::vec3 pitchAxis { glm::cross(glm::normalize(glm::vec3 {rotation.x, rotation.y, 0.0f}), upVector) };

    float sensitivity = Settings::get_instance().GetMouseSensitivity();
    int pitchMultiplier = Settings::get_instance().IsMouseReversed() ? -1 : 1;

    rotation = glm::vec4(rotation, 0.0f) * glm::rotate(sensitivity * deltaX, rotationAxis);

    glm::vec3 tempRotation = rotation;
    rotation = glm::vec4(rotation, 0.0f) * glm::rotate(pitchMultiplier * sensitivity * deltaY, pitchAxis);

    if (glm::abs(glm::dot(glm::normalize(rotation), upVector)) > 0.9)
    {
        rotation = tempRotation;
    }

    SetCameraRot(cameraId, rotation.x, rotation.y, rotation.z);
}

GAME_MODULE()
{
    struct GameModuleInfo info = {};
    info.onFrame = OnFrame;
    info.onEngineStart = OnEngineStart;
    info.onEngineStop = OnEngineStop;
    info.onKeyDown = OnKeyDown;
    info.onKeyUp = OnKeyUp;
    info.onMouseKeyDown = OnMouseKeyDown;
    info.onMouseKeyUp = OnMouseKeyUp;
    info.onMouseMove = OnMouseMove;
    RegisterGameModule(info);
    return true;
}
