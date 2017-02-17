#pragma once

#include <Mathematics/glm.hpp>
#include <Mathematics/gtx/transform.hpp>
#include <Control/Settings.hpp>

class Camera
{
	Camera(void)
	{
		m_Position = glm::vec3(0.0f, 0.0f, 0.0f);
		m_LookAt = glm::vec3(1.0f, 0.0f, 0.0f);
		m_Rotation = m_LookAt - m_Position;

		const Settings* settings = Settings::GetInstance();

		m_FieldOfView = settings->GetFieldOfView();
		m_AspectRatio = settings->GetAspectRatio();
		m_NearClip = 0.1f;
		m_FarClip = 10000.0f;
		m_Perspective = glm::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
		m_Rotation = m_LookAt - m_Position;

		m_IsViewMatrixDirty = true;
	}

public:
	static Camera* GetInstance(void)
	{
		static Camera* instance = nullptr;
		if(instance == nullptr) {
			instance = new Camera();
		}
		return instance;
	}

	// Position
	void SetPosition(const glm::vec3& pos)
	{
		m_Position = pos;
		m_IsViewMatrixDirty = true;
	}

	// Rotation
	void SetRotation(const glm::vec3& rotation)
	{
		m_Rotation = rotation;
		m_LookAt = m_Position + m_Rotation;
		m_IsViewMatrixDirty = true;
	}

	void SetLookAt(const glm::vec3& lookAt)
	{
		m_LookAt = lookAt;
		m_Rotation = m_LookAt - m_Position;
		m_IsViewMatrixDirty = true;
	}

	// Settings
	void SetFieldOfView(const float fov)
	{
		m_FieldOfView = fov;
	}

	void SetAspectRatio(const float aspect)
	{
		m_AspectRatio = aspect;
	}

	void SetClippingDistances(const float nearDistance = 0.1f, const float farDistance = 1000.0f)
	{
		m_NearClip = nearDistance;
		m_FarClip = farDistance;
	}

	void UpdatePerspectiveMatrix(void)
	{
		m_Perspective = glm::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
	}

	// Feedback
	glm::mat4 GetViewMatrix(void)
	{
		if (!m_IsViewMatrixDirty) {
			return m_ViewMatrix;
		}

		glm::vec3 upVector(0.0f, 0.0f, 1.0f);
		m_ViewMatrix = glm::lookAt(m_Position, m_LookAt, upVector);
		m_IsViewMatrixDirty = false;
		return m_ViewMatrix;
	}

	glm::mat4 GetProjectionMatrix(void)
	{
		return m_Perspective;
	}

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
	glm::vec3 m_LookAt;
	glm::mat4 m_Perspective;
};
