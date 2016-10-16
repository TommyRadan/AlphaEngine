#include "Model.hpp"
#include "OpenGL\OpenGL.hpp"

Model::Model(void) :
	m_Position		{ Math::Vector3(0.0f, 0.0f, 0.0f) },
	m_Rotation		{ Math::Vector3(0.0f, 0.0f, 0.0f) },
	m_Scale			{ Math::Vector3(1.0f, 1.0f, 1.0f) },
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

const Math::Matrix4 Model::GetModelMatrix(void)
{
	if (!m_IsModelDirty) return m_ModelMatrix;

	Math::Matrix4 posMatrix = Math::Matrix4::Translate(m_Position);
	Math::Matrix4 rotXMatrix = Math::Matrix4::RotateX(m_Rotation.X);
	Math::Matrix4 rotYMatrix = Math::Matrix4::RotateY(m_Rotation.Y);
	Math::Matrix4 rotZMatrix = Math::Matrix4::RotateZ(m_Rotation.Z);
	Math::Matrix4 scaleMatrix = Math::Matrix4::Scale(m_Scale);
	Math::Matrix4 rotMatrix = rotZMatrix * rotYMatrix * rotXMatrix;

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
		auto vector = Math::Vector4(
			element.second.R / 255.0f, 
			element.second.G / 255.0f, 
			element.second.B / 255.0f, 
			element.second.A / 255.0f
		);

		renderer->UploadVector4(element.first, vector);
	}

	int uploadedTextureReferences = 0;
	for (auto& element : m_Material.Textures) {
		renderer->UploadTextureReference(element.first, uploadedTextureReferences);
		OpenGL::Context::GetInstance()->BindTexture(*(element.second), uploadedTextureReferences);
		uploadedTextureReferences++;
	}

	m_Geometry->Draw();
	renderer->StopRenderer();
}

const Math::Vector3 Model::GetPos(void) const
{
	return m_Position;
}

void Model::SetPos(const Math::Vector3& pos)
{
	m_IsModelDirty = true;
	m_Position = pos;
}

const Math::Vector3 Model::GetRot(void) const
{
	return m_Rotation;
}

void Model::SetRot(const Math::Vector3& rot)
{
	m_IsModelDirty = true;
	m_Rotation = rot;
}

const Math::Vector3 Model::GetScale(void) const
{
	return m_Scale;
}

void Model::SetScale(const Math::Vector3& scale)
{
	m_IsModelDirty = true;
	m_Scale = scale;
}
