#include <RenderingEngine/Camera.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <Mathematics/gtx/transform.hpp>
#include <Infrastructure/Settings.hpp>

RenderingEngine::Camera::Camera()
{
    m_Position = glm::vec3(0.0f, 0.0f, 0.0f);
    m_Rotation = glm::vec3(1.0f, 0.0f, 0.0f);

    const Settings* settings = Settings::GetInstance();

    m_FieldOfView = settings->GetFieldOfView();
    m_AspectRatio = settings->GetAspectRatio();
    m_NearClip = 0.1f;
    m_FarClip = 10000.0f;
    m_Perspective = glm::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);

    m_IsViewMatrixDirty = true;
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

void RenderingEngine::Camera::SetPosition(const glm::vec3& pos)
{
	m_Position = pos;

	if (m_Position.z < 0.5f)
	{
		m_Position.z = 0.5f;
	}

    m_IsViewMatrixDirty = true;
}

void RenderingEngine::Camera::SetRotation(const glm::vec3& rotation)
{
    m_Rotation = glm::normalize(rotation);
    m_IsViewMatrixDirty = true;
}

glm::vec3 RenderingEngine::Camera::GetPosition()
{
    return m_Position;
}

glm::vec3 RenderingEngine::Camera::GetRotation()
{
    return m_Rotation;
}

glm::mat4 RenderingEngine::Camera::GetViewMatrix()
{
    if (!m_IsViewMatrixDirty)
    {
        return m_ViewMatrix;
    }

    glm::vec3 upVector(0.0f, 0.0f, 1.0f);
    glm::vec3 lookAt = m_Position + m_Rotation;
    m_ViewMatrix = glm::lookAt(m_Position, lookAt, upVector);
    m_IsViewMatrixDirty = false;
    return m_ViewMatrix;
}

glm::mat4 RenderingEngine::Camera::GetProjectionMatrix()
{
    return m_Perspective;
}
