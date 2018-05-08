#include <RenderingEngine/OpenGL/Texture.hpp>

#define PUSHSTATE() GLint restoreId; glGetIntegerv( GL_TEXTURE_BINDING_2D, &restoreId );
#define POPSTATE() glBindTexture( GL_TEXTURE_2D, restoreId );

RenderingEngine::OpenGL::Texture::Texture()
{
	glGenTextures(1, &m_ObjectID);
}

RenderingEngine::OpenGL::Texture::~Texture()
{
	glDeleteTextures(1, &m_ObjectID);
}

const GLuint RenderingEngine::OpenGL::Texture::Handle() const
{
	return m_ObjectID;
}

void RenderingEngine::OpenGL::Texture::Image2D(const GLvoid* data,
											   const DataType type,
											   const Format format,
											   const unsigned int width,
											   const unsigned int height,
											   const InternalFormat internalFormat)
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, width, height, 0, (GLenum)format, (GLenum)type, data);

	POPSTATE()
}

void RenderingEngine::OpenGL::Texture::SetWrappingS(const Wrapping s)
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)s);

	POPSTATE()
}

void RenderingEngine::OpenGL::Texture::SetWrappingT(const RenderingEngine::OpenGL::Wrapping t)
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t);

	POPSTATE()
}

void RenderingEngine::OpenGL::Texture::SetWrappingR(const RenderingEngine::OpenGL::Wrapping r)
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, (GLint)r);

	POPSTATE()
}

void RenderingEngine::OpenGL::Texture::SetFilters(const RenderingEngine::OpenGL::Filter min,
                                                  const RenderingEngine::OpenGL::Filter mag)
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)min);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)mag);

	POPSTATE()
}

void RenderingEngine::OpenGL::Texture::GenerateMipmaps()
{
	PUSHSTATE()

	glBindTexture(GL_TEXTURE_2D, m_ObjectID);
	glGenerateMipmap(GL_TEXTURE_2D);

	POPSTATE()
}
