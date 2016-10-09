#pragma once

#include "Typedef.hpp"

#include "VertexArray.hpp"
#include "Texture.hpp"
#include "Framebuffer.hpp"
#include "Program.hpp"
#include "Renderbuffer.hpp"
#include "Shader.hpp"
#include "VertexArray.hpp"
#include "VertexBuffer.hpp"

#include <Utilities\Color.hpp>
#include <Utilities\Exception.hpp>
#include <Utilities\Singleton.hpp>

namespace OpenGL
{
	class OGL : public Singleton<OGL>
	{
		friend Singleton<OGL>;
		OGL(void);

		bool m_IsInit;

	public:
		void Init(void);
		void Quit(void);

		void Enable(const OpenGL::Capability capability);
		void Disable(const OpenGL::Capability capability);

		void ClearColor(const Color& col);
		void Clear(const OpenGL::Buffer buffers = OpenGL::Buffer::Color | OpenGL::Buffer::Depth);

		void DepthMask(const bool writeEnabled);

		void BindTexture(const OpenGL::Texture& texture, const unsigned char unit);

		void BindFramebuffer(const OpenGL::Framebuffer& framebuffer);
		void BindFramebuffer(void);

		void DrawArrays(const OpenGL::VertexArray& vao, const OpenGL::Primitive mode, const unsigned int offset, const unsigned int vertices);
		void DrawElements(const OpenGL::VertexArray& vao, const OpenGL::Primitive mode, const intptr_t offset, const unsigned int count, const unsigned int type);
	};
}
