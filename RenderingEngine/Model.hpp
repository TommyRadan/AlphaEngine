#pragma once

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

	void SetGeometry(Geometry* const geometry);
	const Geometry* GetGeometry(void) const;

	Material& GetMaterial(void);

	const Math::Matrix4 GetModelMatrix(void);
	void Render(Renderer* const renderer);

	const Math::Vector3 GetPos(void) const;
	void SetPos(const Math::Vector3& pos);
	const Math::Vector3 GetRot(void) const;
	void SetRot(const Math::Vector3& rot);
	const Math::Vector3 GetScale(void) const;
	void SetScale(const Math::Vector3& scale);

private:
	Math::Vector3 m_Position;
	Math::Vector3 m_Rotation;
	Math::Vector3 m_Scale;

	Math::Matrix4 m_ModelMatrix;
	bool m_IsModelDirty;

	Geometry* m_Geometry;
	Material m_Material;
};
