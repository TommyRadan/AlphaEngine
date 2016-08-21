#pragma once

#include "Typedef.hpp"

namespace OpenGL
{
	class Renderbuffer
	{
	public:
		Renderbuffer(void);
		~Renderbuffer(void);

		const GLuint Handle(void) const;

		void Storage(const unsigned int width, const unsigned int height, const InternalFormat format);

	private:
		GLuint m_ObjectID;
	};
}
