#include <RenderingEngine/Camera.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <Mathematics/gtx/transform.hpp>
#include <Infrastructure/Settings.hpp>

RenderingEngine::Camera::Camera()
{
    const Settings* settings = Settings::GetInstance();

    m_FieldOfView = settings->GetFieldOfView();
    m_AspectRatio = settings->GetAspectRatio();
    m_NearClip = 0.1f;
    m_FarClip = 10000.0f;

    m_IsViewMatrixDirty = true;
    m_IsPerspectiveMatrixDirty = true;
}

RenderingEngine::Camera* RenderingEngine::Camera::GetInstance()
{
    static Camera* instance = nullptr;

    if(instance == nullptr)
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

    m_Perspective = glm::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
    m_IsPerspectiveMatrixDirty = false;
    return m_Perspective;
}
