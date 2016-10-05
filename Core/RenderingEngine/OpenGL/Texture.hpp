#pragma once

// OpenGL
#include "Typedef.hpp"

namespace OpenGL
{
	class Texture
	{
	public:
		Texture(void);
		~Texture(void);

		const GLuint Handle(void) const;

		void Image2D(const GLvoid* data, const DataType type, const Format format, const unsigned int width, const unsigned int height, const InternalFormat internalFormat);

		void SetWrappingS(const Wrapping s);
		void SetWrappingT(const Wrapping t);
		void SetWrappingR(const Wrapping r);

		void SetFilters(const Filter min, const Filter mag);

		void GenerateMipmaps(void);

	private:
		GLuint m_ObjectID;
	};
}
