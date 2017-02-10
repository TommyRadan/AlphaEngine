#pragma once

#include <Mathematics/Math.hpp>
#include <Utilities/Singleton.hpp>
#include <Control/Settings.hpp>

class Camera :
		public Singleton<Camera>
{
	template<typename Camera>
	friend class Singleton;
	Camera(void)
	{
		m_Position = Math::Vector3(0.0f, 0.0f, 0.0f);
		m_LookAt = Math::Vector3(1.0f, 0.0f, 0.0f);

		const Settings* settings = Settings::GetInstance();

		m_FieldOfView = settings->GetFieldOfView();
		m_AspectRatio = settings->GetAspectRatio();
		m_NearClip = 0.1f;
		m_FarClip = 10000.f;
		m_Perspective = Math::Matrix4::Perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);

		m_IsViewMatrixDirty = true;
	}

public:
	// Position
	void SetPosition(const Math::Vector3& pos)
	{
		m_Position = pos;
		m_IsViewMatrixDirty = true;
	}

	// Rotation
	void SetLookAt(const Math::Vector3& lookAt)
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
		m_Perspective = Math::Matrix4::Perspective(m_FieldOfView, m_AspectRatio, m_NearClip, m_FarClip);
	}

	// Feedback
	Math::Matrix4 GetViewMatrix(void)
	{
		if (!m_IsViewMatrixDirty) {
			return m_ViewMatrix;
		}

		Math::Vector3 upVector(0.0f, 0.0f, 1.0f);
		m_ViewMatrix = Math::Matrix4::LookAt(m_Position, m_LookAt, upVector);
		m_IsViewMatrixDirty = false;
		return m_ViewMatrix;
	}

	Math::Matrix4 GetProjectionMatrix(void)
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
	Math::Matrix4 m_ViewMatrix;
	bool m_IsViewMatrixDirty;

	// Data
	Math::Vector3 m_Position;
	Math::Vector3 m_LookAt;
	Math::Matrix4 m_Perspective;
};
