#include "Shader.hpp"

#include <stdio.h>

namespace OpenGL
{
	Shader::Shader(const ShaderType shaderType)
	{
		m_ObjectID = glCreateShader(GLenum(shaderType));
	}

	Shader::~Shader(void)
	{
		glDeleteShader(m_ObjectID);
	}

	const GLuint Shader::Handle(void) const
	{
		return m_ObjectID;
	}

	void Shader::Source(const std::string& code)
	{
		const char* c = code.c_str();
		const int length = (int)code.length();
		glShaderSource(m_ObjectID, 1, &c, &length);
	}

	void Shader::Compile(void)
	{
		GLint res;
		glCompileShader(m_ObjectID);
		glGetShaderiv(m_ObjectID, GL_COMPILE_STATUS, &res);

		if (res != GL_TRUE) {
			throw Exception(GetInfoLog());
		}
	}

	std::string Shader::GetInfoLog(void)
	{
		GLint res;
		glGetShaderiv(m_ObjectID, GL_INFO_LOG_LENGTH, &res);

		if (res > 0) {
			std::string infoLog(res, 0);
			glGetShaderInfoLog(m_ObjectID, res, &res, &infoLog[0]);
			return infoLog;
		} else {
			return "";
		}
	}
}
