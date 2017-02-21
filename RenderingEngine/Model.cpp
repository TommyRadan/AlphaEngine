#include "Model.hpp"
#include "Renderers/Renderer.hpp"
#include "RenderingEngine.hpp"
#include <Mathematics/glm.hpp>
#include <Mathematics/gtx/transform.hpp>

Model::Model(void) :
	m_Position		{ glm::vec3(0.0f, 0.0f, 0.0f) },
	m_Rotation		{ glm::vec3(0.0f, 0.0f, 0.0f) },
	m_Scale			{ glm::vec3(1.0f, 1.0f, 1.0f) },
	m_IsModelDirty	{ true },
	m_Geometry		{ nullptr }
{}

void Model::SetGeometry(Geometry* const geometry)
{
	m_Geometry = geometry;
}

const Geometry* Model::GetGeometry(void) const
{
	return m_Geometry;
}

Material& Model::GetMaterial(void)
{
	return m_Material;
}

const glm::mat4 Model::GetModelMatrix(void)
{
	if (!m_IsModelDirty) return m_ModelMatrix;

	glm::mat4 posMatrix = glm::translate(m_Position);
	glm::mat4 rotXMatrix = glm::rotate(m_Rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 rotYMatrix = glm::rotate(m_Rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 rotZMatrix = glm::rotate(m_Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
	glm::mat4 scaleMatrix = glm::scale(m_Scale);
	glm::mat4 rotMatrix = rotZMatrix * rotYMatrix * rotXMatrix;

	m_ModelMatrix = posMatrix * rotMatrix * scaleMatrix;
	m_IsModelDirty = false;
	return m_ModelMatrix;
}

void Model::Render(void)
{
	if (m_Geometry == nullptr) {
		throw Exception("Attempt to render model without geometry!");
	}

	Renderer* r = (Renderer*)RenderingEngine::Context::GetInstance()->GetActiveRenderer();

	if (r == nullptr) {
		throw Exception("Attempted to render model without active renderer!");
	}

	r->UploadMatrix4("modelMatrix", this->GetModelMatrix());
	r->SetupCamera();
    r->SetupMaterial(m_Material);
	static_cast<Geometry*>(m_Geometry)->Draw();
}

const glm::vec3 Model::GetPosition(void) const
{
	return m_Position;
}

void Model::SetPosition(const glm::vec3& position)
{
	m_IsModelDirty = true;
	m_Position = position;
}

const glm::vec3 Model::GetRotation(void) const
{
	return m_Rotation;
}

void Model::SetRotation(const glm::vec3& rotation)
{
	m_IsModelDirty = true;
	m_Rotation = rotation;
}

const glm::vec3 Model::GetScale(void) const
{
	return m_Scale;
}

void Model::SetScale(const glm::vec3& scale)
{
	m_IsModelDirty = true;
	m_Scale = scale;
}
