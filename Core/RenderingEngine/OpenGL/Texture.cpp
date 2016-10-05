#include "Texture.hpp"

#define PUSHSTATE() GLint restoreId; glGetIntegerv( GL_TEXTURE_BINDING_2D, &restoreId );
#define POPSTATE() glBindTexture( GL_TEXTURE_2D, restoreId );

namespace OpenGL
{
	Texture::Texture(void)
	{
		glGenTextures(1, &m_ObjectID);
	}

	Texture::~Texture(void)
	{
		glDeleteTextures(1, &m_ObjectID);
	}

	const GLuint Texture::Handle(void) const
	{
		return m_ObjectID;
	}

	void Texture::Image2D(const GLvoid* data, const DataType type, const Format format, const unsigned int width, const unsigned int height, const InternalFormat internalFormat)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internalFormat, width, height, 0, (GLenum)format, (GLenum)type, data);

		POPSTATE()
	}

	void Texture::SetWrappingS(const Wrapping s)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)s);

		POPSTATE()
	}

	void Texture::SetWrappingT(const Wrapping t)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t);

		POPSTATE()
	}

	void Texture::SetWrappingR(const Wrapping r)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, (GLint)r);

		POPSTATE()
	}

	void Texture::SetFilters(const Filter min, const Filter mag)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)min);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)mag);

		POPSTATE()
	}

	void Texture::GenerateMipmaps(void)
	{
		PUSHSTATE()

		glBindTexture(GL_TEXTURE_2D, m_ObjectID);
		glGenerateMipmap(GL_TEXTURE_2D);

		POPSTATE()
	}
}

