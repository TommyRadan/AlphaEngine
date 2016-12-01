#pragma once

#include "Typedef.hpp"
#include "Texture.hpp"

#include <Utilities/Exception.hpp>

namespace OpenGL
{
	class Framebuffer
	{
	public:
		Framebuffer(
			const unsigned int width, 
			const unsigned int height, 
			const unsigned char color = 32u,
			const unsigned char depth = 24u
		);
		~Framebuffer(void);

		const unsigned int Handle(void) const;

		const Texture& GetTexture(void) const;
		const Texture& GetDepthTexture(void) const;

	private:
		Framebuffer(const Framebuffer&);
		const Framebuffer& operator=(const Framebuffer&);

		unsigned int m_ObjectID;
		Texture m_ColorTexture;
		Texture m_DepthTexture;
	};
}
