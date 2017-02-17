#include "Model.hpp"
#include "OpenGL/OpenGL.hpp"

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

void Model::Render(Renderer* const renderer)
{
	if (m_Geometry == nullptr) {
		throw Exception("Attempt to render model without geometry!");
	}

	if (renderer == nullptr) {
		throw Exception("Attempt to render model without renderer!");
	}

	renderer->StartRenderer();
	renderer->UploadMatrix4("modelMatrix", this->GetModelMatrix());
	renderer->UploadMatrix4("viewMatrix", Camera::GetInstance()->GetViewMatrix());
	renderer->UploadMatrix4("projectionMatrix", Camera::GetInstance()->GetProjectionMatrix());

	for (auto& element : m_Material.Coefficients) {
		renderer->UploadCoefficient(element.first, element.second);
	}

	for (auto& element : m_Material.Colors) {
		auto vector = glm::vec4(
			element.second.R / 255.0f, 
			element.second.G / 255.0f, 
			element.second.B / 255.0f, 
			element.second.A / 255.0f
		);

		renderer->UploadVector4(element.first, vector);
	}

	uint8_t uploadedTextureReferences = 0u;
	for (auto& element : m_Material.Textures) {
		renderer->UploadTextureReference(element.first, uploadedTextureReferences);
		OpenGL::Context::GetInstance()->BindTexture(
				*static_cast<OpenGL::Texture*>(element.second),
				uploadedTextureReferences
		);
		uploadedTextureReferences++;
	}

	m_Geometry->Draw();
	renderer->StopRenderer();
}

const glm::vec3 Model::GetPos(void) const
{
	return m_Position;
}

void Model::SetPos(const glm::vec3& pos)
{
	m_IsModelDirty = true;
	m_Position = pos;
}

const glm::vec3 Model::GetRot(void) const
{
	return m_Rotation;
}

void Model::SetRot(const glm::vec3& rot)
{
	m_IsModelDirty = true;
	m_Rotation = rot;
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
