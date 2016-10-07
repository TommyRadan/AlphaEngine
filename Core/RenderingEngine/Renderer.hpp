#pragma once

#include "OpenGL\Shader.hpp"
#include "OpenGL\Program.hpp"

#include "OpenGL\Texture.hpp"
#include "Camera.hpp"

#include <Mathematics\Math.hpp>

class Renderer
{
protected:
	Renderer() :
		m_VertexShader(OpenGL::ShaderType::Vertex),
		m_FragmentShader(OpenGL::ShaderType::Fragment)
	{}

	OpenGL::Shader m_VertexShader;
	OpenGL::Shader m_FragmentShader;
	OpenGL::Program m_Program;

public:
	void StartRenderer(void)
	{
		m_Program.Start();
	}

	void UploadTextureReference(const std::string& textureName, const int position)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(textureName);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, position);
	}

	void UploadCoefficient(const std::string& coefficientName, const float coefficient)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(coefficientName);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, coefficient);
	}

	void UploadMatrix3(const std::string& mat3Name, const Math::Matrix3& matrix)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(mat3Name);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, matrix);
	}

	void UploadMatrix4(const std::string& mat4Name, const Math::Matrix4& matrix)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(mat4Name);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, matrix);
	}

	void UploadVector2(const std::string& vec2Name, const Math::Vector2& vector)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(vec2Name);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, vector);
	}

	void UploadVector3(const std::string& vec3Name, const Math::Vector3& vector)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(vec3Name);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, vector);
	}

	void UploadVector4(const std::string& vec4Name, const Math::Vector4& vector)
	{
		OpenGL::Uniform uniform = m_Program.GetUniform(vec4Name);
		if (uniform == -1) {
			return;
		}
		m_Program.SetUniform(uniform, vector);
	}

	void StopRenderer(void)
	{
		m_Program.Stop();
	}
};
