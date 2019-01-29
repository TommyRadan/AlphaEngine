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

#include <RenderingEngine/Camera.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>
#include <Infrastructure/Settings.hpp>

RenderingEngine::Camera::Camera()
{
    const Settings* settings = Settings::GetInstance();

    fieldOfView = settings->GetFieldOfView();
    aspectRatio = settings->GetAspectRatio();
    nearClip = 0.1f;
    farClip = 10000.0f;

    m_IsViewMatrixDirty = true;
    m_IsPerspectiveMatrixDirty = true;

    transform.SetRotation({1.0f, 0.0f, 0.0f});
}

RenderingEngine::Camera* RenderingEngine::Camera::GetInstance()
{
    static Camera* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Camera();
    }

    return instance;
}

void RenderingEngine::Camera::InvalidateViewMatrix()
{
    m_IsViewMatrixDirty = true;
}

const glm::mat4 RenderingEngine::Camera::GetViewMatrix() const
{
    if (!m_IsViewMatrixDirty)
    {
        return m_ViewMatrix;
    }

    glm::vec3 upVector(0.0f, 0.0f, 1.0f);
    glm::vec3 lookAt = transform.GetPosition() + transform.GetRotation();
    m_ViewMatrix = glm::lookAt(transform.GetPosition(), lookAt, upVector);
    m_IsViewMatrixDirty = false;
    return m_ViewMatrix;
}

void RenderingEngine::Camera::InvalidateProjectionMatrix()
{
    m_IsPerspectiveMatrixDirty = true;
}

const glm::mat4 RenderingEngine::Camera::GetProjectionMatrix() const
{
    if (!m_IsPerspectiveMatrixDirty)
    {
        return m_Perspective;
    }

    m_Perspective = glm::perspective(fieldOfView, aspectRatio, nearClip, farClip);
    m_IsPerspectiveMatrixDirty = false;
    return m_Perspective;
}
