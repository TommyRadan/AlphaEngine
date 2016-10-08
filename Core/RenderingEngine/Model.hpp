#pragma once

#include <type_traits>

#include <Mathematics\Math.hpp>

#include <Utilities\Exception.hpp>

#include "Geometry.hpp"
#include "Material.hpp"
#include "Camera.hpp"

#include "Renderer.hpp"

class Model
{
public:
	Model(void);

	void SetGeometry(Geometry* const geometry) {
		m_Geometry = geometry;
	}

	void SetRenderer(Renderer* const renderer) {
		m_Renderer = renderer;
	}

	const Geometry* GetGeometry(void) const {
		return m_Geometry;
	}

	Material& GetMaterial(void) {
		return m_Material;
	}

	const Math::Matrix4 GetModelMatrix(void);
	void Render(void);

	const Math::Vector3 GetPos(void) const {
		return m_Position;
	}

	void SetPos(const Math::Vector3& pos) {
		m_IsModelDirty = true;
		m_Position = pos;
	}

	const Math::Vector3 GetRot(void) const {
		return m_Rotation;
	}

	void SetRot(const Math::Vector3& rot) {
		m_IsModelDirty = true;
		m_Rotation = rot;
	}

	const Math::Vector3 GetScale(void) const {
		return m_Scale;
	}

	void SetScale(const Math::Vector3& scale) {
		m_IsModelDirty = true;
		m_Scale = scale;
	}

private:
	Math::Vector3 m_Position;
	Math::Vector3 m_Rotation;
	Math::Vector3 m_Scale;

	Math::Matrix4 m_ModelMatrix;
	bool m_IsModelDirty;

	Geometry* m_Geometry;
	Renderer* m_Renderer;
	Material m_Material;
};
