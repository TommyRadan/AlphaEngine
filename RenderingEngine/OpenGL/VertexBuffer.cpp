/**
 * Copyright (c) 2018 Tomislav Radanovic
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

#include <RenderingEngine/OpenGL/VertexBuffer.hpp>

RenderingEngine::OpenGL::VertexBuffer::VertexBuffer()
{
	glGenBuffers(1, &m_ObjectID);
}

RenderingEngine::OpenGL::VertexBuffer::~VertexBuffer()
{
	glDeleteBuffers(1, &m_ObjectID);
}

const GLuint RenderingEngine::OpenGL::VertexBuffer::Handle() const
{
	return m_ObjectID;
}

void RenderingEngine::OpenGL::VertexBuffer::Data(const void* data,
												 const size_t length,
												 const RenderingEngine::OpenGL::BufferUsage usage)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
	glBufferData(GL_ARRAY_BUFFER, length, data, (GLenum)usage);
}

void RenderingEngine::OpenGL::VertexBuffer::ElementData(const void* data,
												 		const size_t length,
												 		const RenderingEngine::OpenGL::BufferUsage usage)
{
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ObjectID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, length, data, (GLenum)usage);
}

void RenderingEngine::OpenGL::VertexBuffer::SubData(const void* data, const size_t offset, const size_t length)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
	glBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
}

void RenderingEngine::OpenGL::VertexBuffer::GetSubData(void* data, const size_t offset, const size_t length)
{
	glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
	glGetBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
}
