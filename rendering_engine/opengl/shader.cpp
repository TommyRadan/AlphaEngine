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
#include <rendering_engine/opengl/shader.hpp>

rendering_engine::opengl::shader::shader(const shader_type type)
{
    m_object_id = glCreateShader(GLenum(type));
    if (m_object_id == 0)
    {
        LOG_FTL("Cannot create shader (type=0x%X)", static_cast<unsigned>(type));
        throw std::runtime_error{"Shader creation failed!"};
    }
    LOG_INF("Created shader id=%u type=0x%X", m_object_id, static_cast<unsigned>(type));
}

rendering_engine::opengl::shader::~shader()
{
    glDeleteShader(m_object_id);
}

const GLuint rendering_engine::opengl::shader::handle() const
{
    return m_object_id;
}

void rendering_engine::opengl::shader::source(const std::string& code)
{
    const char* c = code.c_str();
    auto length = (int)code.length();
    m_code = code;
    glShaderSource(m_object_id, 1, &c, &length);
}

void rendering_engine::opengl::shader::compile()
{
    GLint res;
    glCompileShader(m_object_id);
    glGetShaderiv(m_object_id, GL_COMPILE_STATUS, &res);

    if (res != GL_TRUE)
    {
        LOG_ERR("Shader compile failed (id=%u)", m_object_id);
        LOG_ERR("Shader code: \n%s", m_code.c_str());
        LOG_ERR("Info log: \n%s", get_info_log().c_str());
        LOG_FTL("Cannot compile shader");
        throw std::runtime_error{get_info_log()};
    }

    LOG_INF("Shader compiled successfully (id=%u)", m_object_id);
}

std::string rendering_engine::opengl::shader::get_info_log()
{
    GLint res;
    glGetShaderiv(m_object_id, GL_INFO_LOG_LENGTH, &res);

    if (res <= 0)
        return "";

    std::string info_log(res, 0);
    glGetShaderInfoLog(m_object_id, res, &res, &info_log[0]);
    return info_log;
}
