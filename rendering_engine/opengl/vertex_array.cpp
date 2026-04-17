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

#include <rendering_engine/opengl/vertex_array.hpp>

rendering_engine::opengl::vertex_array::vertex_array()
{
    glGenVertexArrays(1, &m_object_id);
}

rendering_engine::opengl::vertex_array::~vertex_array()
{
    glDeleteVertexArrays(1, &m_object_id);
}

const uint32_t rendering_engine::opengl::vertex_array::handle() const
{
    return m_object_id;
}

void rendering_engine::opengl::vertex_array::bind_attribute(const rendering_engine::opengl::attribute& attribute,
                                                            const rendering_engine::opengl::vertex_buffer& buffer,
                                                            const rendering_engine::opengl::type type,
                                                            unsigned int count,
                                                            unsigned int stride,
                                                            unsigned long long offset)
{
    glBindVertexArray(m_object_id);
    glBindBuffer(GL_ARRAY_BUFFER, buffer.handle());
    glEnableVertexAttribArray(attribute);
    glVertexAttribPointer(attribute, count, (GLenum)type, GL_FALSE, stride, (GLvoid*)offset);
}

void rendering_engine::opengl::vertex_array::bind_elements(const rendering_engine::opengl::vertex_buffer& elements)
{
    glBindVertexArray(m_object_id);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elements.handle());
}
