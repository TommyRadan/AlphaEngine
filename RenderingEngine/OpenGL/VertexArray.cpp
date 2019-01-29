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

#include <RenderingEngine/OpenGL/VertexArray.hpp>

RenderingEngine::OpenGL::VertexArray::VertexArray()
{
	glGenVertexArrays(1, &m_ObjectID);
}

RenderingEngine::OpenGL::VertexArray::~VertexArray()
{
	glDeleteVertexArrays(1, &m_ObjectID);
}

const GLuint RenderingEngine::OpenGL::VertexArray::Handle() const
{
	return m_ObjectID;
}

void RenderingEngine::OpenGL::VertexArray::BindAttribute(const RenderingEngine::OpenGL::Attribute& attribute,
								const RenderingEngine::OpenGL::VertexBuffer& buffer,
								const RenderingEngine::OpenGL::Type type,
								unsigned int count,
								unsigned int stride,
								unsigned long long offset
) {
	glBindVertexArray(m_ObjectID);
	glBindBuffer(GL_ARRAY_BUFFER, buffer.Handle());
	glEnableVertexAttribArray(attribute);
	glVertexAttribPointer(attribute, count, (GLenum)type, GL_FALSE, stride, (GLvoid*)offset);
}

void RenderingEngine::OpenGL::VertexArray::BindElements(const RenderingEngine::OpenGL::VertexBuffer& elements)
{
	glBindVertexArray(m_ObjectID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elements.Handle());
}
