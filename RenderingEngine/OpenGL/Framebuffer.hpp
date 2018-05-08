#pragma once

#include <RenderingEngine/OpenGL/Typedef.hpp>
#include <RenderingEngine/OpenGL/Texture.hpp>

namespace RenderingEngine
{
	namespace OpenGL
	{
		struct Framebuffer
		{
			Framebuffer(
					uint32_t width,
					uint32_t height,
					uint8_t color = 32u,
					uint8_t depth = 24u
			);
			~Framebuffer();

			Framebuffer(const Framebuffer&) = delete;
			const Framebuffer& operator=(const Framebuffer&) = delete;

			const uint32_t Handle() const;

			const Texture& GetTexture() const;
			const Texture& GetDepthTexture() const;

		private:
			uint32_t m_ObjectID;
			Texture m_ColorTexture;
			Texture m_DepthTexture;
		};
	}
}
