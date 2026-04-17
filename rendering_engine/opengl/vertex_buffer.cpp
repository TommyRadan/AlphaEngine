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

#include <rendering_engine/opengl/vertex_buffer.hpp>

rendering_engine::opengl::vertex_buffer::vertex_buffer()
{
    glGenBuffers(1, &m_object_id);
}

rendering_engine::opengl::vertex_buffer::~vertex_buffer()
{
    glDeleteBuffers(1, &m_object_id);
}

const unsigned int rendering_engine::opengl::vertex_buffer::handle() const
{
    return m_object_id;
}

void rendering_engine::opengl::vertex_buffer::data(const void* data,
                                                 const size_t length,
                                                 const rendering_engine::opengl::buffer_usage usage)
{
    glBindBuffer(GL_ARRAY_BUFFER, m_object_id);
    glBufferData(GL_ARRAY_BUFFER, length, data, (GLenum)usage);
}

void rendering_engine::opengl::vertex_buffer::element_data(const void* data,
                                                        const size_t length,
                                                        const rendering_engine::opengl::buffer_usage usage)
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_object_id);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, length, data, (GLenum)usage);
}

void rendering_engine::opengl::vertex_buffer::sub_data(const void* data, const size_t offset, const size_t length)
{
    glBindBuffer(GL_ARRAY_BUFFER, m_object_id);
    glBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
}

void rendering_engine::opengl::vertex_buffer::get_sub_data(void* data, const size_t offset, const size_t length)
{
    glBindBuffer(GL_ARRAY_BUFFER, m_object_id);
    glGetBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
}
