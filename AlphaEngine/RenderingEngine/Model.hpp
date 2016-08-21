#pragma once

#include <type_traits>

#include <Mathematics\glm.hpp>
#include <Mathematics\gtx\transform.hpp>

#include <Utilities\Exception.hpp>

#include "Geometry.hpp"
#include "Material.hpp"
#include "Camera.hpp"

#include "Renderer.hpp"

class Model
{
public:
	Model();

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

	const Math::mat4 GetModelMatrix(void);
	void Render(void);

	const Math::vec3 GetPos(void) const {
		return m_Position;
	}

	void SetPos(const Math::vec3& pos) {
		m_IsModelDirty = true;
		m_Position = pos;
	}

	const Math::vec3 GetRot(void) const {
		return m_Rotation;
	}

	void SetRot(const Math::vec3& rot) {
		m_IsModelDirty = true;
		m_Rotation = rot;
	}

	const Math::vec3 GetScale(void) const {
		return m_Scale;
	}

	void SetScale(const Math::vec3& scale) {
		m_IsModelDirty = true;
		m_Scale = scale;
	}

private:
	Math::vec3 m_Position;
	Math::vec3 m_Rotation;
	Math::vec3 m_Scale;

	Math::mat4 m_ModelMatrix;
	bool m_IsModelDirty;

	Geometry* m_Geometry;
	Renderer* m_Renderer;
	Material m_Material;
};
