/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include "API/GameModule.hpp"
#include "API/Time.hpp"
#include "API/Log.hpp"
#include "API/Camera.hpp"

#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/Settings.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/vector_angle.hpp>

CameraId cameraId = 0;

static void OnEngineStart()
{
    cameraId = CreateCamera(CameraType::Perspective);
    AttachCamera(cameraId);

    SetCameraPos(cameraId, -5.0, 0.0, 0.0);
}

static void OnEngineStop()
{
    DestroyCamera(cameraId);
}

static void OnFrame()
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
    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::SHIFT))
    {
        speed = 30.0f;
    }

    float distance = speed * ((float)GetDeltaTime() / 1000);
    glm::vec3 upVector {0.0f, 0.0f, 1.0f};
    glm::vec3 newPosition = position;

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::W))
    {
        newPosition += rotation * distance;
    }

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::A))
    {
        newPosition -= glm::cross(rotation, upVector) * distance;
    }

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::S))
    {
        newPosition -= rotation * distance;
    }

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::D))
    {
        newPosition += glm::cross(rotation, upVector) * distance;
    }

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::SPACE))
    {
        newPosition += upVector * distance;
    }

    if (EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::CTRL))
    {
        newPosition -= upVector * distance;
    }

    SetCameraPos(cameraId, newPosition.x, newPosition.y, newPosition.z);
}

static void OnMouseMove(int32_t deltaX, int32_t deltaY)
{
#if _DEBUG
    if (!EventEngine::Dispatch::get_instance().IsKeyPressed(EventEngine::KeyCode::MOUSE_LEFT))
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
    struct GameModuleInfo info;
    info.onFrame = OnFrame;
    info.onEngineStart = OnEngineStart;
    info.onEngineStop = OnEngineStop;
    info.onMouseMove = OnMouseMove;
    RegisterGameModule(info);
    return true;
}
