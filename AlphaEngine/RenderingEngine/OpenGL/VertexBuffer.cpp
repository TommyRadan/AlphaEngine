#include "VertexBuffer.hpp"

namespace OpenGL
{
	VertexBuffer::VertexBuffer()
	{
		glGenBuffers(1, &m_ObjectID);
	}

	VertexBuffer::~VertexBuffer()
	{
		glDeleteBuffers(1, &m_ObjectID);
	}

	const GLuint VertexBuffer::Handle(void) const
	{
		return m_ObjectID;
	}

	void VertexBuffer::Data(const void* data, const size_t length, const BufferUsage usage)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
		glBufferData(GL_ARRAY_BUFFER, length, data, (GLenum)usage);
	}

	void VertexBuffer::SubData(const void* data, const size_t offset, const size_t length)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
		glBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
	}

	void VertexBuffer::GetSubData(void* data, const size_t offset, const size_t length)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_ObjectID);
		glGetBufferSubData(GL_ARRAY_BUFFER, offset, length, data);
	}
}
