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

#include <stdexcept>

#include <RenderingEngine/OpenGL/Program.hpp>
#include <Infrastructure/log.hpp>

RenderingEngine::OpenGL::Program::Program()
{
	m_ObjectID = glCreateProgram();
}

RenderingEngine::OpenGL::Program::~Program()
{
	glDeleteProgram(m_ObjectID);
}

const GLuint RenderingEngine::OpenGL::Program::Handle() const
{
	return m_ObjectID;
}

void RenderingEngine::OpenGL::Program::Start()
{
	glUseProgram(m_ObjectID);
}

void RenderingEngine::OpenGL::Program::Stop()
{
	glUseProgram(0);
}

void RenderingEngine::OpenGL::Program::Attach(const Shader& shader)
{
	glAttachShader(m_ObjectID, shader.Handle());
}

void RenderingEngine::OpenGL::Program::Link()
{
	GLint status;
	glLinkProgram(m_ObjectID);
	glGetProgramiv(m_ObjectID, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		LOG_FTL("Cannot link program");
		LOG_FTL("%s", this->GetInfoLog().c_str());
		throw std::runtime_error { this->GetInfoLog() };
	}
}

std::string RenderingEngine::OpenGL::Program::GetInfoLog()
{
	GLint res;
	glGetProgramiv(m_ObjectID, GL_INFO_LOG_LENGTH, &res);

	if (res > 0)
	{
		std::string infoLog(res, 0);
		glGetProgramInfoLog(m_ObjectID, res, &res, &infoLog[0]);
		return infoLog;
	}
	else
	{
		return "";
	}
}

RenderingEngine::OpenGL::Attribute RenderingEngine::OpenGL::Program::GetAttribute(const std::string& name)
{
	std::map<std::string, Uniform>::iterator it;

	it = this->m_Attributes.find(name);
	if (it != this->m_Attributes.end()) {
		return it->second;
	}

	Uniform location = glGetAttribLocation(m_ObjectID, name.c_str());;
	this->m_Attributes[name] = location;
	return location;
}

RenderingEngine::OpenGL::Uniform RenderingEngine::OpenGL::Program::GetUniform(const std::string& name)
{
	std::map<std::string, Uniform>::iterator it;

	it = this->m_Uniforms.find(name);
	if (it != this->m_Uniforms.end()) {
		return it->second;
	}

	Uniform location = glGetUniformLocation(m_ObjectID, name.c_str());;
	this->m_Uniforms[name] = location;
	return location;
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const int value)
{
	glUniform1i(uniform, value);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const float value)
{
	glUniform1f(uniform, value);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec2& value)
{
	glUniform2f(uniform, value.x, value.y);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec3& value)
{
	glUniform3f(uniform, value.x, value.y, value.z);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec4& value)
{
	glUniform4f(uniform, value.x, value.y, value.z, value.w);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const float* values, const unsigned int count)
{
	glUniform1fv(uniform, count, values);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec2* values, const unsigned int count)
{
	glUniform2fv(uniform, count, (float*)values);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec3* values, const unsigned int count)
{
	glUniform3fv(uniform, count, (float*)values);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::vec4* values, const unsigned int count)
{
	glUniform4fv(uniform, count, (float*)values);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::mat3& value)
{
	glUniformMatrix3fv(uniform, 1, GL_FALSE, &value[0][0]);
}

void RenderingEngine::OpenGL::Program::SetUniform(const Uniform& uniform, const glm::mat4& value)
{
	glUniformMatrix4fv(uniform, 1, GL_FALSE, &value[0][0]);
}
