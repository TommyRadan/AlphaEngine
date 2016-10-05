#pragma once

// OpenGL Mathematics
#include <Mathematics\glm.hpp>
#include <Mathematics\gtx\transform.hpp>

#include <Utilities\Singleton.hpp>

class Camera : public Singleton<Camera>
{
	friend Singleton<Camera>;
	Camera()
	{
		m_Position = Math::vec3(0.0f, 0.0f, 0.0f);
		m_LookAt = Math::vec3(1.0f, 0.0f, 0.0f);

		m_FieldOfView = 1.22f; 		// Defaulting to 70 degrees
		m_AspectRatio = 1.78f; 		// Defaulting 16/9
		m_NearClip = 0.1f; 			// Defaulting to 10cm
		m_FarClip = 10000.f; 		// Defaulting to 10km
		m_Perspective = Math::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);

		m_IsViewMatrixDirty = true;
	}

public:
	// Position
	void SetPosition(const Math::vec3& pos) 
	{
		m_Position = pos;
		m_IsViewMatrixDirty = true;
	}

	// Rotation
	void SetLookAt(const Math::vec3& lookAt)
	{
		m_LookAt = lookAt;
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
		m_Perspective = Math::perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
	}

	// Feedback
	Math::mat4 GetViewMatrix(void)
	{
		if (!m_IsViewMatrixDirty) {
			return m_ViewMatrix;
		}

		Math::vec3 upVector(0.0f, 0.0f, 1.0f);
		m_ViewMatrix = Math::lookAt(m_Position, m_LookAt, upVector);
		m_IsViewMatrixDirty = false;
		return m_ViewMatrix;
	}

	Math::mat4 GetProjectionMatrix(void)
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
	Math::mat4 m_ViewMatrix;
	bool m_IsViewMatrixDirty;

	// Data
	Math::vec3 m_Position;
	Math::vec3 m_LookAt;
	Math::mat4 m_Perspective;
};
