#include <RenderingEngine/OpenGL/Framebuffer.hpp>
#include <Infrastructure/Exception.hpp>

#define PUSHSTATE() GLint restoreId; glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &restoreId );
#define POPSTATE() glBindFramebuffer( GL_DRAW_FRAMEBUFFER, restoreId );

RenderingEngine::OpenGL::Framebuffer::Framebuffer(const uint32_t width,
												  const uint32_t height,
												  const uint8_t color,
												  const uint8_t depth)
{
	PUSHSTATE()

	// Determine appropriate formats
	InternalFormat colorFormat;
	if (color == 24) colorFormat = InternalFormat::RGB;
	else if (color == 32) colorFormat = InternalFormat::RGBA;
	else throw Exception("Framebuffer could not be created!");

	InternalFormat depthFormat;
	if (depth == 8) depthFormat = InternalFormat::DepthComponent;
	else if (depth == 16) depthFormat = InternalFormat::DepthComponent16;
	else if (depth == 24) depthFormat = InternalFormat::DepthComponent24;
	else if (depth == 32) depthFormat = InternalFormat::DepthComponent32F;
	else throw Exception("Framebuffer could not be created!");

	// Create FBO
	glGenFramebuffers(1, &m_ObjectID);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ObjectID);

	// Create texture to hold color buffer
	m_ColorTexture.Image2D(nullptr, DataType::UnsignedByte, Format::RGBA, width, height, colorFormat);
	m_ColorTexture.SetWrappingT(OpenGL::Wrapping::ClampEdge);
	m_ColorTexture.SetWrappingS(OpenGL::Wrapping::ClampEdge);
	m_ColorTexture.SetFilters(OpenGL::Filter::Linear, OpenGL::Filter::Linear);
	glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture.Handle(), 0);

	// Create renderbuffer to hold depth buffer
	if (depth > 0U) {
		glBindTexture(GL_TEXTURE_2D, m_DepthTexture.Handle());
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint)depthFormat, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, 0);
		m_DepthTexture.SetWrappingT(OpenGL::Wrapping::ClampEdge);
		m_DepthTexture.SetWrappingS(OpenGL::Wrapping::ClampEdge);
		m_DepthTexture.SetFilters(OpenGL::Filter::Nearest, OpenGL::Filter::Nearest);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture.Handle(), 0);
	}

	// Check
	if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		throw Exception("Framebuffer could not be created!");
	}

	POPSTATE()
}

RenderingEngine::OpenGL::Framebuffer::~Framebuffer()
{
	glDeleteFramebuffers(1, &m_ObjectID);
}

const uint32_t RenderingEngine::OpenGL::Framebuffer::Handle() const
{
	return m_ObjectID;
}

const RenderingEngine::OpenGL::Texture& RenderingEngine::OpenGL::Framebuffer::GetTexture() const
{
	return m_ColorTexture;
}

const RenderingEngine::OpenGL::Texture& RenderingEngine::OpenGL::Framebuffer::GetDepthTexture() const
{
	return m_DepthTexture;
}
