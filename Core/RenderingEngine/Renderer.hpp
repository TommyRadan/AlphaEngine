#pragma once

#include "OpenGL\Shader.hpp"
#include "OpenGL\Program.hpp"

#include "OpenGL\Texture.hpp"
#include "Camera.hpp"

#include <Mathematics\Math.hpp>

class Renderer
{
protected:
	Renderer(void) :
		m_IsInit{ false }
	{}

	virtual void Init(void) = 0;
	virtual void Quit(void) = 0;
	bool m_IsInit;

	OpenGL::Shader* m_VertexShader;
	OpenGL::Shader* m_FragmentShader;
	OpenGL::Program* m_Program;

public:
	void StartRenderer(void)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::StartRenderer called before Renderer::Init");
		}

		m_Program->Start();
	}

	void UploadTextureReference(const std::string& textureName, const int position)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadTextureReference called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(textureName);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + textureName + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, position);
	}

	void UploadCoefficient(const std::string& coefficientName, const float coefficient)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadCoefficient called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(coefficientName);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + coefficientName + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, coefficient);
	}

	void UploadMatrix3(const std::string& mat3Name, const Math::Matrix3& matrix)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadMatrix3 called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(mat3Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + mat3Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, matrix);
	}

	void UploadMatrix4(const std::string& mat4Name, const Math::Matrix4& matrix)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadMatrix4 called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(mat4Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + mat4Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, matrix);
	}

	void UploadVector2(const std::string& vec2Name, const Math::Vector2& vector)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadVector2 called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec2Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec2Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void UploadVector3(const std::string& vec3Name, const Math::Vector3& vector)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadVector3 called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec3Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec3Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void UploadVector4(const std::string& vec4Name, const Math::Vector4& vector)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::UploadVector4 called before Renderer::Init");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec4Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec4Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void StopRenderer(void)
	{
		if (!m_IsInit) {
			throw Exception("Renderer::StopRenderer called before Renderer::Init");
		}

		m_Program->Stop();
	}
};
