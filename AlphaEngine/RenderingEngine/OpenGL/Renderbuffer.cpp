#include "Renderbuffer.hpp"

namespace OpenGL
{
	Renderbuffer::Renderbuffer(void)
	{
		glGenRenderbuffers(1, &m_ObjectID);
	}

	Renderbuffer::~Renderbuffer(void)
	{
		glDeleteRenderbuffers(1, &m_ObjectID);
	}

	const GLuint Renderbuffer::Handle(void) const
	{
		return m_ObjectID;
	}

	void Renderbuffer::Storage(const unsigned int width, const unsigned int height, const InternalFormat format)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, m_ObjectID);
		glRenderbufferStorage(GL_RENDERBUFFER, (GLenum)format, width, height);
	}
}
