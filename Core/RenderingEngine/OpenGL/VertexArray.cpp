#include "VertexArray.hpp"

namespace OpenGL
{
	VertexArray::VertexArray(void)
	{
		glGenVertexArrays(1, &m_ObjectID);
	}

	VertexArray::~VertexArray(void)
	{
		glDeleteVertexArrays(1, &m_ObjectID);
	}

	const GLuint VertexArray::Handle(void) const
	{
		return m_ObjectID;
	}

	void VertexArray::BindAttribute(
		const Attribute& attribute, 
		const VertexBuffer& buffer, 
		Type type, 
		unsigned int count, 
		unsigned int stride, 
		unsigned long long offset
	) {
		glBindVertexArray(m_ObjectID);
		glBindBuffer(GL_ARRAY_BUFFER, buffer.Handle());
		glEnableVertexAttribArray(attribute);
		glVertexAttribPointer(attribute, count, (GLenum)type, GL_FALSE, stride, (GLvoid*)offset);
	}

	void VertexArray::BindElements(const VertexBuffer& elements)
	{
		glBindVertexArray(m_ObjectID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elements.Handle());
	}
}
