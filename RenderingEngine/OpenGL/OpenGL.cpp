#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <Infrastructure/Exception.hpp>

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
    if (glewInit() != GLEW_OK)
    {
        throw Exception("Could not initialize OpenGL");
    }

    int versionMajor, versionMinor;
    glGetIntegerv(GL_MAJOR_VERSION, &versionMajor);
    glGetIntegerv(GL_MINOR_VERSION, &versionMinor);

    if(versionMajor < 3 || (versionMajor == 3 && versionMinor < 3))
    {
        throw Exception("OpenGL version error! Unsupported hardware or driver");
    }
}

void RenderingEngine::OpenGL::Context::Quit()
{}

void RenderingEngine::OpenGL::Context::Enable(const RenderingEngine::OpenGL::Capability capability)
{
    glEnable((GLenum)capability);
}

void RenderingEngine::OpenGL::Context::Disable(const RenderingEngine::OpenGL::Capability capability)
{
    glDisable((GLenum)capability);
}

void RenderingEngine::OpenGL::Context::ClearColor(const Infrastructure::Color& color)
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
                                                    const unsigned int type)
{
    glBindVertexArray(vao.Handle());
    glDrawElements((GLenum)mode, count, type, (const GLvoid*)offset);
    glBindVertexArray(0);
}
