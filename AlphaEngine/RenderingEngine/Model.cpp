#include "Model.hpp"

Model::Model() :
	m_Position	{ Math::vec3(0.0f, 0.0f, 0.0f) },
	m_Rotation	{ Math::vec3(0.0f, 0.0f, 0.0f) },
	m_Scale		{ Math::vec3(1.0f, 1.0f, 1.0f) }
{
	m_IsModelDirty = true;

	m_Geometry = nullptr;
	m_Renderer = nullptr;
}

const Math::mat4 Model::GetModelMatrix()
{
	if (!m_IsModelDirty) return m_ModelMatrix;

	Math::mat4 posMatrix = Math::translate(m_Position);
	Math::mat4 rotXMatrix = Math::rotate(m_Rotation.x, Math::vec3(1.0f, 0.0f, 0.0f));
	Math::mat4 rotYMatrix = Math::rotate(m_Rotation.y, Math::vec3(0.0f, 1.0f, 0.0f));
	Math::mat4 rotZMatrix = Math::rotate(m_Rotation.z, Math::vec3(0.0f, 0.0f, 1.0f));
	Math::mat4 scaleMatrix = Math::scale(m_Scale);
	Math::mat4 rotMatrix = rotZMatrix * rotYMatrix * rotXMatrix;

	m_ModelMatrix = posMatrix * rotMatrix * scaleMatrix;
	m_IsModelDirty = false;
	return m_ModelMatrix;
}

void Model::Render()
{
	if (m_Geometry == nullptr) {
		throw Exception("Attempt to render model without geometry!");
	}

	if (m_Renderer == nullptr) {
		throw Exception("Attempt to render model without renderer!");
	}

	m_Renderer->StartRenderer();
	m_Renderer->UploadMatrix4("modelMatrix", this->GetModelMatrix());
	m_Renderer->UploadMatrix4("viewMatrix", Camera::GetInstance()->GetViewMatrix());
	m_Renderer->UploadMatrix4("projectionMatrix", Camera::GetInstance()->GetProjectionMatrix());

	for (auto& element : m_Material.Coefficients) {
		m_Renderer->UploadCoefficient(element.first, element.second);
	}

	for (auto& element : m_Material.Colors) {
		auto vector = Math::vec4(
			element.second.R / 255.0f, 
			element.second.G / 255.0f, 
			element.second.B / 255.0f, 
			element.second.A / 255.0f
		);

		m_Renderer->UploadVector4(element.first, vector);
	}

	int uploadedTextureReferences = 0;
	for (auto& element : m_Material.Textures) {
		m_Renderer->UploadTextureReference(element.first, uploadedTextureReferences);
		OpenGL::Context::GetInstance()->BindTexture(*(element.second), uploadedTextureReferences);
		uploadedTextureReferences++;
	}

	m_Geometry->Draw();
	m_Renderer->StopRenderer();
}
