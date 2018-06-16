#pragma once

#include <Mathematics/glm.hpp>

namespace Infrastructure
{
    struct Transform
    {
        Transform();

        void SetPosition(const glm::vec3& position);
        void SetRotation(const glm::vec3& rotation);
        void SetScale(const glm::vec3& scale);

        glm::vec3 GetPosition() const;
        glm::vec3 GetRotation() const;
        glm::vec3 GetScale() const;

        glm::mat4 GetTransformMatrix() const;

    private:
        mutable glm::mat4 m_TransformMatrix;
        mutable bool m_IsTransformMatrixDirty;

        glm::vec3 m_Position;
        glm::vec3 m_Rotation;
        glm::vec3 m_Scale;
    };
}
