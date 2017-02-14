#include "OpenGL.hpp"

namespace OpenGL
{
	Context::Context(void)
	{ m_IsInit = false; }

	void Context::Init(void)
	{
		if (m_IsInit) {
			throw Exception("Called Context::Init twice");
		}

		if (!gl3wInit()) {
			throw Exception("Could not gather OpenGL function pointers!");
		}
		if (!gl3wIsSupported(3, 3)) {
			throw Exception("OpenGL 3.3 is the oldest version supported!");
		}
	}

	void Context::Quit(void)
	{}

	void Context::Enable(const OpenGL::Capability capability)
	{
		glEnable((GLenum)capability);
	}

	void Context::Disable(const OpenGL::Capability capability)
	{
		glDisable((GLenum)capability);
	}

	void Context::ClearColor(const Color& col)
	{
		glClearColor(col.R / 255.0f, col.G / 255.0f, col.B / 255.0f, col.A / 255.0f);
	}

	void Context::Clear(const OpenGL::Buffer buffers)
	{
		glClear((GLbitfield)buffers);
	}

	void Context::DepthMask(const bool writeEnabled)
	{
		glDepthMask(writeEnabled ? GL_TRUE : GL_FALSE);
	}

	void Context::BindTexture(const OpenGL::Texture& texture, const unsigned char unit)
	{
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, texture.Handle());
	}

	void Context::BindFramebuffer(const OpenGL::Framebuffer& framebuffer)
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.Handle());

		// Set viewport to frame buffer size
		GLint obj, width, height;
		glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj);

		GLint res;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &res);
		glBindTexture(GL_TEXTURE_2D, obj);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
		glBindTexture(GL_TEXTURE_2D, res);

		glViewport(0, 0, width, height);
	}

	void Context::BindFramebuffer(void)
	{
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		// Set viewport to default frame buffer size
		//glViewport(m_DefaultViewport[0], m_DefaultViewport[1], m_DefaultViewport[2], m_DefaultViewport[3]);
	}

	void Context::DrawArrays(const OpenGL::VertexArray& vao, const OpenGL::Primitive mode, const unsigned int offset, const unsigned int vertices)
	{
		glBindVertexArray(vao.Handle());
		glDrawArrays((GLenum)mode, offset, vertices);
		glBindVertexArray(0);
	}

	void Context::DrawElements(const OpenGL::VertexArray& vao, const OpenGL::Primitive mode, const intptr_t offset, const unsigned int count, const unsigned int type)
	{
		glBindVertexArray(vao.Handle());
		glDrawElements((GLenum)mode, count, type, (const GLvoid*)offset);
		glBindVertexArray(0);
	}
}
