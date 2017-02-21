#pragma once

#include <Mathematics/glm.hpp>
#include <Utilities/Exception.hpp>

#include "Material.hpp"
#include "Geometry.hpp"

struct Model
{
	Model(void);

	void SetGeometry(Geometry* const geometry);
	const Geometry* GetGeometry(void) const;

	Material& GetMaterial(void);

	const glm::mat4 GetModelMatrix(void);
	void Render(void);

	const glm::vec3 GetPosition(void) const;
	void SetPosition(const glm::vec3& position);
	const glm::vec3 GetRotation(void) const;
	void SetRotation(const glm::vec3& rotation);
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
