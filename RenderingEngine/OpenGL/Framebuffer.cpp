/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <RenderingEngine/OpenGL/Framebuffer.hpp>
#include <Infrastructure/log.hpp>
#include <stdexcept>

#define PUSHSTATE() GLint restoreId; glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &restoreId );
#define POPSTATE() glBindFramebuffer( GL_DRAW_FRAMEBUFFER, restoreId );

RenderingEngine::OpenGL::Framebuffer::Framebuffer(const uint32_t width,
												  const uint32_t height,
												  const uint8_t color,
												  const uint8_t depth) :
	m_ObjectID { 0 }
{
	PUSHSTATE()

	// Determine appropriate formats
	InternalFormat colorFormat;
	if (color == 24) colorFormat = InternalFormat::RGB;
	else if (color == 32) colorFormat = InternalFormat::RGBA;
	else {
        LOG_ERR("Framebuffer could not be created, color size not supported (%u)", color);
        POPSTATE()
        return;
	}

	InternalFormat depthFormat;
	if (depth == 8) depthFormat = InternalFormat::DepthComponent;
	else if (depth == 16) depthFormat = InternalFormat::DepthComponent16;
	else if (depth == 24) depthFormat = InternalFormat::DepthComponent24;
	else if (depth == 32) depthFormat = InternalFormat::DepthComponent32F;
	else {
        LOG_ERR("Framebuffer could not be created, depth size not supported (%u)", depth);
        POPSTATE()
        return;
    }

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
	    LOG_ERR("Framebuffer could not be created, unknown reason (0x%X)",
                  glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
		throw std::runtime_error {"Framebuffer could not be created!"};
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
