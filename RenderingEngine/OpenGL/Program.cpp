#include "Program.hpp"

namespace OpenGL
{
	Program::Program(void)
	{
		m_ObjectID = glCreateProgram();
	}

	Program::~Program(void)
	{
		glDeleteProgram(m_ObjectID);
	}

	const GLuint Program::Handle(void) const
	{
		return m_ObjectID;
	}

	void Program::Start(void)
	{
		glUseProgram(m_ObjectID);
	}

	void Program::Stop(void)
	{
		glUseProgram(0);
	}

	void Program::Attach(const Shader& shader)
	{
		glAttachShader(m_ObjectID, shader.Handle());
	}

	void Program::Link(void)
	{
		GLint status;
		glLinkProgram(m_ObjectID);
		glGetProgramiv(m_ObjectID, GL_LINK_STATUS, &status);

		if (status != GL_TRUE) {
			throw Exception(this->GetInfoLog());
		}
	}

	std::string Program::GetInfoLog(void)
	{
		GLint res;
		glGetProgramiv(m_ObjectID, GL_INFO_LOG_LENGTH, &res);

		if (res > 0) {
			std::string infoLog(res, 0);
			glGetProgramInfoLog(m_ObjectID, res, &res, &infoLog[0]);
			return infoLog;
		} else {
			return "";
		}
	}

	Attribute Program::GetAttribute(const std::string& name)
	{
		return glGetAttribLocation(m_ObjectID, name.c_str());
	}

	Uniform Program::GetUniform(const std::string& name)
	{
		return glGetUniformLocation(m_ObjectID, name.c_str());
	}

	void Program::SetUniform(const Uniform& uniform, const int value)
	{
		glUniform1i(uniform, value);
	}

	void Program::SetUniform(const Uniform& uniform, const float value)
	{
		glUniform1f(uniform, value);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec2& value)
	{
		glUniform2f(uniform, value.x, value.y);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec3& value)
	{
		glUniform3f(uniform, value.x, value.y, value.z);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec4& value)
	{
		glUniform4f(uniform, value.x, value.y, value.z, value.w);
	}

	void Program::SetUniform(const Uniform& uniform, const float* values, const unsigned int count)
	{
		glUniform1fv(uniform, count, values);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec2* values, const unsigned int count)
	{
		glUniform2fv(uniform, count, (float*)values);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec3* values, const unsigned int count)
	{
		glUniform3fv(uniform, count, (float*)values);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::vec4* values, const unsigned int count)
	{
		glUniform4fv(uniform, count, (float*)values);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::mat3& value)
	{
		glUniformMatrix3fv(uniform, 1, GL_FALSE, &value[0][0]);
	}

	void Program::SetUniform(const Uniform& uniform, const glm::mat4& value)
	{
		glUniformMatrix4fv(uniform, 1, GL_FALSE, &value[0][0]);
	}
}
