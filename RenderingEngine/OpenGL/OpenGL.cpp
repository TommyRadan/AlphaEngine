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

#include <stdexcept>

#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <Infrastructure/Log.hpp>

RenderingEngine::OpenGL::Context* RenderingEngine::OpenGL::Context::GetInstance()
{
    static Context *instance { nullptr };

    if (instance == nullptr)
    {
        instance = new Context();
    }

    return instance;
}

void RenderingEngine::OpenGL::Context::Init()
{
    LOG_INFO("Init RenderingEngine::OpenGL");

    if (glewInit() != GLEW_OK)
    {
        LOG_FATAL("Could not initialize OpenGL");
        throw std::runtime_error{"Could not initialize OpenGL"};
    }

    int versionMajor, versionMinor;
    glGetIntegerv(GL_MAJOR_VERSION, &versionMajor);
    glGetIntegerv(GL_MINOR_VERSION, &versionMinor);

    if(versionMajor < 3 || (versionMajor == 3 && versionMinor < 3))
    {
        LOG_FATAL("Could not initialize OpenGL, supported version is %i.%i", versionMajor, versionMinor);
        throw std::runtime_error{"OpenGL version error! Unsupported hardware or driver"};
    }
}

void RenderingEngine::OpenGL::Context::Quit()
{
    LOG_INFO("Quit RenderingEngine::OpenGL");
}

RenderingEngine::OpenGL::Framebuffer*
RenderingEngine::OpenGL::Context::CreateFramebuffer(
    uint32_t width, uint32_t height,
    uint8_t color, uint8_t depth)
{
	return new RenderingEngine::OpenGL::Framebuffer(width, height, color, depth);
}

RenderingEngine::OpenGL::Program* RenderingEngine::OpenGL::Context::CreateProgram()
{
    return new OpenGL::Program();
}

RenderingEngine::OpenGL::Shader* RenderingEngine::OpenGL::Context::CreateShader(ShaderType type)
{
    return new OpenGL::Shader(type);
}

RenderingEngine::OpenGL::Texture* RenderingEngine::OpenGL::Context::CreateTexture()
{
    return new OpenGL::Texture();
}

RenderingEngine::OpenGL::VertexArray* RenderingEngine::OpenGL::Context::CreateVAO()
{
    return new OpenGL::VertexArray();
}

RenderingEngine::OpenGL::VertexBuffer* RenderingEngine::OpenGL::Context::CreateVBO()
{
    return new OpenGL::VertexBuffer();
}

void RenderingEngine::OpenGL::Context::DeleteFramebuffer(const OpenGL::Framebuffer* fb)
{
    delete fb;
}

void RenderingEngine::OpenGL::Context::DeleteProgram(const OpenGL::Program* program)
{
    delete program;
}

void RenderingEngine::OpenGL::Context::DeleteShader(const OpenGL::Shader* shader)
{
    delete shader;
}

void RenderingEngine::OpenGL::Context::DeleteTexture(const OpenGL::Texture* texture)
{
    delete texture;
}

void RenderingEngine::OpenGL::Context::DeleteVAB(const OpenGL::VertexArray* vao)
{
    delete vao;
}

void RenderingEngine::OpenGL::Context::DeleteVBO(const OpenGL::VertexBuffer* vbo)
{
    delete vbo;
}

void RenderingEngine::OpenGL::Context::Enable(const RenderingEngine::OpenGL::Capability capability)
{
    glEnable((GLenum)capability);
}

void RenderingEngine::OpenGL::Context::Disable(const RenderingEngine::OpenGL::Capability capability)
{
    glDisable((GLenum)capability);
}

void RenderingEngine::OpenGL::Context::ClearColor(const RenderingEngine::Util::Color& color)
{
    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void RenderingEngine::OpenGL::Context::Clear(const OpenGL::Buffer buffers)
{
    glClear((GLbitfield)buffers);
}

void RenderingEngine::OpenGL::Context::DepthMask(const bool writeEnabled)
{
    glDepthMask(writeEnabled ? GL_TRUE : GL_FALSE);
}

void RenderingEngine::OpenGL::Context::BindTexture(const RenderingEngine::OpenGL::Texture& texture, const unsigned char unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture.Handle());
}

void RenderingEngine::OpenGL::Context::BindFramebuffer(const RenderingEngine::OpenGL::Framebuffer& framebuffer)
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

void RenderingEngine::OpenGL::Context::BindFramebuffer()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Set viewport to default frame buffer size
    //glViewport(m_DefaultViewport[0], m_DefaultViewport[1], m_DefaultViewport[2], m_DefaultViewport[3]);

    /*
     * TODO: Fix glViewport for returning to default framebuffer
     */
}

void RenderingEngine::OpenGL::Context::DrawArrays(const RenderingEngine::OpenGL::VertexArray& vao,
                                                  const RenderingEngine::OpenGL::Primitive mode,
                                                  const unsigned int offset,
                                                  const size_t vertices)
{
    glBindVertexArray(vao.Handle());
    glDrawArrays((GLenum)mode, offset, (GLsizei)vertices);
    glBindVertexArray(0);
}

void RenderingEngine::OpenGL::Context::DrawElements(const RenderingEngine::OpenGL::VertexArray& vao,
                                                    const RenderingEngine::OpenGL::Primitive mode,
                                                    const intptr_t offset,
                                                    const unsigned int count,
                                                    const RenderingEngine::OpenGL::Type type)
{
    glBindVertexArray(vao.Handle());
    glDrawElements((GLenum)mode, count, (GLenum)type, (const GLvoid*)offset);
    glBindVertexArray(0);
}
