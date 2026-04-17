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

#include <infrastructure/log.hpp>
#include <rendering_engine/opengl/program.hpp>

rendering_engine::opengl::program::program()
{
    m_object_id = glCreateProgram();
    if (m_object_id == 0)
    {
        LOG_ERR("glCreateProgram returned 0 (program creation failed)");
    }
    else
    {
        LOG_INF("Created GL program id=%u", m_object_id);
    }
}

rendering_engine::opengl::program::~program()
{
    glDeleteProgram(m_object_id);
}

const uint32_t rendering_engine::opengl::program::handle() const
{
    return m_object_id;
}

void rendering_engine::opengl::program::start()
{
    glUseProgram(m_object_id);
}

void rendering_engine::opengl::program::stop()
{
    glUseProgram(0);
}

void rendering_engine::opengl::program::attach(const shader& shader)
{
    glAttachShader(m_object_id, shader.handle());
}

void rendering_engine::opengl::program::link()
{
    GLint status;
    glLinkProgram(m_object_id);
    glGetProgramiv(m_object_id, GL_LINK_STATUS, &status);

    if (status != GL_TRUE)
    {
        LOG_ERR("Program link failed (id=%u)", m_object_id);
        LOG_ERR("Info log: \n%s", this->get_info_log().c_str());
        LOG_FTL("Cannot link program");
        throw std::runtime_error{this->get_info_log()};
    }

    LOG_INF("GL program linked successfully (id=%u)", m_object_id);
}

std::string rendering_engine::opengl::program::get_info_log()
{
    GLint res;
    glGetProgramiv(m_object_id, GL_INFO_LOG_LENGTH, &res);

    if (res > 0)
    {
        std::string info_log(res, 0);
        glGetProgramInfoLog(m_object_id, res, &res, &info_log[0]);
        return info_log;
    }
    else
    {
        return "";
    }
}

rendering_engine::opengl::attribute rendering_engine::opengl::program::get_attribute(const std::string& name)
{
    std::map<std::string, uniform>::iterator it;

    it = this->m_attributes.find(name);
    if (it != this->m_attributes.end())
    {
        return it->second;
    }

    uniform location = glGetAttribLocation(m_object_id, name.c_str());
    ;
    this->m_attributes[name] = location;
    return location;
}

rendering_engine::opengl::uniform rendering_engine::opengl::program::get_uniform(const std::string& name)
{
    std::map<std::string, uniform>::iterator it;

    it = this->m_uniforms.find(name);
    if (it != this->m_uniforms.end())
    {
        return it->second;
    }

    uniform location = glGetUniformLocation(m_object_id, name.c_str());
    ;
    this->m_uniforms[name] = location;
    return location;
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const int value)
{
    glUniform1i(uniform, value);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const float value)
{
    glUniform1f(uniform, value);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const glm::vec2& value)
{
    glUniform2f(uniform, value.x, value.y);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const glm::vec3& value)
{
    glUniform3f(uniform, value.x, value.y, value.z);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const glm::vec4& value)
{
    glUniform4f(uniform, value.x, value.y, value.z, value.w);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform,
                                                    const float* values,
                                                    const unsigned int count)
{
    glUniform1fv(uniform, count, values);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform,
                                                    const glm::vec2* values,
                                                    const unsigned int count)
{
    glUniform2fv(uniform, count, (float*)values);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform,
                                                    const glm::vec3* values,
                                                    const unsigned int count)
{
    glUniform3fv(uniform, count, (float*)values);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform,
                                                    const glm::vec4* values,
                                                    const unsigned int count)
{
    glUniform4fv(uniform, count, (float*)values);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const glm::mat3& value)
{
    glUniformMatrix3fv(uniform, 1, GL_FALSE, &value[0][0]);
}

void rendering_engine::opengl::program::set_uniform(const uniform& uniform, const glm::mat4& value)
{
    glUniformMatrix4fv(uniform, 1, GL_FALSE, &value[0][0]);
}
