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

#include <RenderingEngine/Util/Transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/transform.hpp>

RenderingEngine::Util::Transform::Transform() :
        m_IsTransformMatrixDirty { true },
        m_Position { 0.0f, 0.0f, 0.0f },
        m_Rotation { 0.0f, 0.0f, 0.0f },
        m_Scale { 1.0f, 1.0f, 1.0f }
{}

void RenderingEngine::Util::Transform::SetPosition(const glm::vec3& position)
{
    m_Position = position;
    m_IsTransformMatrixDirty = true;
}

void RenderingEngine::Util::Transform::SetRotation(const glm::vec3& rotation)
{
    m_Rotation = rotation;
    m_IsTransformMatrixDirty = true;
}

void RenderingEngine::Util::Transform::SetScale(const glm::vec3& scale)
{
    m_Scale = scale;
    m_IsTransformMatrixDirty = true;
}

glm::vec3 RenderingEngine::Util::Transform::GetPosition() const
{
    return m_Position;
}

glm::vec3 RenderingEngine::Util::Transform::GetRotation() const
{
    return m_Rotation;
}

glm::vec3 RenderingEngine::Util::Transform::GetScale() const
{
    return m_Scale;
}

glm::mat4 RenderingEngine::Util::Transform::GetTransformMatrix() const
{
    if (!m_IsTransformMatrixDirty)
    {
        return m_TransformMatrix;
    }

    glm::mat4 posMatrix = glm::translate(m_Position);
    glm::mat4 rotXMatrix = glm::rotate(m_Rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotYMatrix = glm::rotate(m_Rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZMatrix = glm::rotate(m_Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 scaleMatrix = glm::scale(m_Scale);
    glm::mat4 rotMatrix = rotZMatrix * rotYMatrix * rotXMatrix;

    m_TransformMatrix = posMatrix * rotMatrix * scaleMatrix;
    m_IsTransformMatrixDirty = false;
    return m_TransformMatrix;
}
