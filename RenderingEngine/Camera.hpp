#pragma once

#include <Mathematics/glm.hpp>
#include <Infrastructure/Transform.hpp>

namespace RenderingEngine
{
    class Camera
    {
        Camera();

    public:
        static Camera* GetInstance();

        Infrastructure::Transform transform;

        void InvalidateViewMatrix();
        const glm::mat4 GetViewMatrix() const;

        void InvalidateProjectionMatrix();
        const glm::mat4 GetProjectionMatrix() const;

    private:
        // Settings
        float m_FieldOfView;
        float m_AspectRatio;
        float m_NearClip;
        float m_FarClip;

        // Matrices
        mutable glm::mat4 m_ViewMatrix;
        mutable bool m_IsViewMatrixDirty;
        mutable glm::mat4 m_Perspective;
        mutable bool m_IsPerspectiveMatrixDirty;
    };
}
