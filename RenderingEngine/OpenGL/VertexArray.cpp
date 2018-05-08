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
