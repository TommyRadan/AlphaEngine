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

#include <rendering_engine/opengl/texture.hpp>

#define PUSHSTATE()                                                                                                    \
    GLint restoreId;                                                                                                   \
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &restoreId);
#define POPSTATE() glBindTexture(GL_TEXTURE_2D, restoreId);

rendering_engine::opengl::texture::texture()
{
    glGenTextures(1, &m_object_id);
}

rendering_engine::opengl::texture::~texture()
{
    glDeleteTextures(1, &m_object_id);
}

const uint32_t rendering_engine::opengl::texture::handle() const
{
    return m_object_id;
}

void rendering_engine::opengl::texture::image2_d(const void* data,
                                                 const data_type type,
                                                 const format format,
                                                 const uint32_t width,
                                                 const uint32_t height,
                                                 const internal_format internal_format)
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal_format, width, height, 0, (GLenum)format, (GLenum)type, data);

    POPSTATE()
}

void rendering_engine::opengl::texture::set_wrapping_s(const wrapping s)
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)s);

    POPSTATE()
}

void rendering_engine::opengl::texture::set_wrapping_t(const rendering_engine::opengl::wrapping t)
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t);

    POPSTATE()
}

void rendering_engine::opengl::texture::set_wrapping_r(const rendering_engine::opengl::wrapping r)
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, (GLint)r);

    POPSTATE()
}

void rendering_engine::opengl::texture::set_filters(const rendering_engine::opengl::filter min,
                                                    const rendering_engine::opengl::filter mag)
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)mag);

    POPSTATE()
}

void rendering_engine::opengl::texture::generate_mipmaps()
{
    PUSHSTATE()

    glBindTexture(GL_TEXTURE_2D, m_object_id);
    glGenerateMipmap(GL_TEXTURE_2D);

    POPSTATE()
}
