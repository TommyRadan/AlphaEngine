#pragma once

#include <Mathematics/glm.hpp>

namespace RenderingEngine
{
    class Camera
    {
        Camera();

    public:
        static Camera* GetInstance();

        void SetPosition(const glm::vec3& pos);
        void SetRotation(const glm::vec3& rotation);

        glm::vec3 GetPosition();
        glm::vec3 GetRotation();

        // Feedback
        glm::mat4 GetViewMatrix();
        glm::mat4 GetProjectionMatrix();

    private:
        // Settings
        float m_FieldOfView;
        float m_AspectRatio;
        float m_NearClip;
        float m_FarClip;

        // Feedback
        glm::mat4 m_ViewMatrix;
        bool m_IsViewMatrixDirty;

        // Data
        glm::vec3 m_Position;
        glm::vec3 m_Rotation;
        glm::mat4 m_Perspective;
    };
}
