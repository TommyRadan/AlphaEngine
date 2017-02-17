#pragma once

#include <Mathematics/glm.hpp>
#include <Utilities/Exception.hpp>

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

	const glm::mat4 GetModelMatrix(void);
	void Render(Renderer* const renderer);

	const glm::vec3 GetPos(void) const;
	void SetPos(const glm::vec3& pos);
	const glm::vec3 GetRot(void) const;
	void SetRot(const glm::vec3& rot);
	const glm::vec3 GetScale(void) const;
	void SetScale(const glm::vec3& scale);

private:
	glm::vec3 m_Position;
	glm::vec3 m_Rotation;
	glm::vec3 m_Scale;

	glm::mat4 m_ModelMatrix;
	bool m_IsModelDirty;

	Geometry* m_Geometry;
	Material m_Material;
};
