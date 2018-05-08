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
