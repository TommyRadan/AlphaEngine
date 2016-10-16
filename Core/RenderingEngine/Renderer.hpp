#pragma once

// OpenGL
#include "OpenGL\OpenGL.hpp"

// Mathematics
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

private:
	const bool CheckForBadInit(void)
	{
		if(m_VertexShader == nullptr) return false;
		if(m_FragmentShader == nullptr) return false;
		if(m_Program == nullptr) return false;
		if(m_IsInit == false) return false;
		return true;
	}

public:
	void StartRenderer(void)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::StartRenderer called before proper initialization");
		}

		m_Program->Start();
	}

	void UploadTextureReference(const std::string& textureName, const int position)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadTextureReference called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(textureName);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + textureName + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, position);
	}

	void UploadCoefficient(const std::string& coefficientName, const float coefficient)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadCoefficient called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(coefficientName);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + coefficientName + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, coefficient);
	}

	void UploadMatrix3(const std::string& mat3Name, const Math::Matrix3& matrix)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadMatrix3 called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(mat3Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + mat3Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, matrix);
	}

	void UploadMatrix4(const std::string& mat4Name, const Math::Matrix4& matrix)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadMatrix4 called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(mat4Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + mat4Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, matrix);
	}

	void UploadVector2(const std::string& vec2Name, const Math::Vector2& vector)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadVector2 called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec2Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec2Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void UploadVector3(const std::string& vec3Name, const Math::Vector3& vector)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadVector3 called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec3Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec3Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void UploadVector4(const std::string& vec4Name, const Math::Vector4& vector)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::UploadVector4 called before proper initialization");
		}

		OpenGL::Uniform uniform = m_Program->GetUniform(vec4Name);
		if (uniform == -1) {
			throw Exception("Could not find uniform " + vec4Name + " in StandardRenderer");
		}
		m_Program->SetUniform(uniform, vector);
	}

	void StopRenderer(void)
	{
		if (!CheckForBadInit()) {
			throw Exception("Renderer::StopRenderer called before proper initialization");
		}

		m_Program->Stop();
	}
};
