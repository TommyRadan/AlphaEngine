#include <Infrastructure/Transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <Mathematics/gtx/transform.hpp>

Infrastructure::Transform::Transform() :
        m_IsTransformMatrixDirty { true },
        m_Position { 0.0f, 0.0f, 0.0f },
        m_Rotation { 0.0f, 0.0f, 0.0f },
        m_Scale { 1.0f, 1.0f, 1.0f }
{}

void Infrastructure::Transform::SetPosition(const glm::vec3& position)
{
    m_Position = position;
    m_IsTransformMatrixDirty = true;
}

void Infrastructure::Transform::SetRotation(const glm::vec3& rotation)
{
    m_Rotation = rotation;
    m_IsTransformMatrixDirty = true;
}

void Infrastructure::Transform::SetScale(const glm::vec3& scale)
{
    m_Scale = scale;
    m_IsTransformMatrixDirty = true;
}

glm::vec3 Infrastructure::Transform::GetPosition() const
{
    return m_Position;
}

glm::vec3 Infrastructure::Transform::GetRotation() const
{
    return m_Rotation;
}

glm::vec3 Infrastructure::Transform::GetScale() const
{
    return m_Scale;
}

glm::mat4 Infrastructure::Transform::GetTransformMatrix() const
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
