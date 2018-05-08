#include <RenderingEngine/Renderables/Renderable.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <Mathematics/gtx/transform.hpp>

RenderingEngine::Renderable::Renderable() :
    m_IsModelMatrixDirty { true },
    m_Position { 0.0f, 0.0f, 0.0f },
    m_Rotation { 0.0f, 0.0f, 0.0f },
    m_Scale { 1.0f, 1.0f, 1.0f }
{}

void RenderingEngine::Renderable::SetPosition(const glm::vec3& position)
{
    m_Position = position;
    m_IsModelMatrixDirty = true;
}

void RenderingEngine::Renderable::SetRotation(const glm::vec3& rotation)
{
	m_Rotation = rotation;
    m_IsModelMatrixDirty = true;
}

void RenderingEngine::Renderable::SetScale(const glm::vec3& scale)
{
    m_Scale = scale;
    m_IsModelMatrixDirty = true;
}

glm::vec3 RenderingEngine::Renderable::GetPosition() const
{
    return m_Position;
}

glm::vec3 RenderingEngine::Renderable::GetRotation() const
{
    return m_Rotation;
}

glm::vec3 RenderingEngine::Renderable::GetScale() const
{
    return m_Scale;
}

glm::mat4 RenderingEngine::Renderable::GetModelMatrix() const
{
    if (!m_IsModelMatrixDirty)
    {
        return m_ModelMatrix;
    }

    glm::mat4 posMatrix = glm::translate(m_Position);
    glm::mat4 rotXMatrix = glm::rotate(m_Rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotYMatrix = glm::rotate(m_Rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZMatrix = glm::rotate(m_Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 scaleMatrix = glm::scale(m_Scale);
    glm::mat4 rotMatrix = rotZMatrix * rotYMatrix * rotXMatrix;

    m_ModelMatrix = posMatrix * rotMatrix * scaleMatrix;
    m_IsModelMatrixDirty = false;
    return m_ModelMatrix;
}
