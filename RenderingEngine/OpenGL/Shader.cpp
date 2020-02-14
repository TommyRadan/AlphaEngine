/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <exception>
#include <stdexcept>

#include <RenderingEngine/OpenGL/Shader.hpp>
#include <Infrastructure/Log.hpp>

RenderingEngine::OpenGL::Shader::Shader(const ShaderType shaderType)
{
	m_ObjectID = glCreateShader(GLenum(shaderType));
	if(m_ObjectID == 0)
	{
		LOG_FATAL("Cannot create shader");
		throw std::runtime_error{"Shader creation failed!"};
	}
}

RenderingEngine::OpenGL::Shader::~Shader()
{
	glDeleteShader(m_ObjectID);
}

const GLuint RenderingEngine::OpenGL::Shader::Handle() const
{
	return m_ObjectID;
}

void RenderingEngine::OpenGL::Shader::Source(const std::string& code)
{
	const char* c = code.c_str();
	auto length = (int) code.length();
	m_Code = code;
	glShaderSource(m_ObjectID, 1, &c, &length);
}

void RenderingEngine::OpenGL::Shader::Compile()
{
	GLint res;
	glCompileShader(m_ObjectID);
	glGetShaderiv(m_ObjectID, GL_COMPILE_STATUS, &res);

	if (res != GL_TRUE)
	{
	    LOG_ERROR("Shader code: \n%s", m_Code.c_str());
	    LOG_ERROR("Errors: \n%s", GetInfoLog().c_str());
		LOG_FATAL("Cannot compile shader");
		throw std::runtime_error{GetInfoLog()};
	}
}

std::string RenderingEngine::OpenGL::Shader::GetInfoLog()
{
	GLint res;
	glGetShaderiv(m_ObjectID, GL_INFO_LOG_LENGTH, &res);

	if(res <= 0) return "";

	std::string infoLog(res, 0);
	glGetShaderInfoLog(m_ObjectID, res, &res, &infoLog[0]);
	return infoLog;
}
