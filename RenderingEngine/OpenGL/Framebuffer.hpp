#pragma once

#include "Typedef.hpp"
#include "Texture.hpp"

#include <Utilities\Exception.hpp>

namespace OpenGL
{
	class Framebuffer
	{
	public:
		Framebuffer(
			const unsigned int width, 
			const unsigned int height, 
			const unsigned char color = 32, 
			const unsigned char depth = 24
		);
		~Framebuffer(void);

		const GLuint Handle(void) const;

		const Texture& GetTexture(void) const;
		const Texture& GetDepthTexture(void) const;

	private:
		Framebuffer(const Framebuffer&);
		const Framebuffer& operator=(const Framebuffer&);

		GLuint m_ObjectID;
		Texture m_ColorTexture;
		Texture m_DepthTexture;
	};
}
