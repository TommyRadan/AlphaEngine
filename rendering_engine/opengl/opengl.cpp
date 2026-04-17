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

#include <SDL3/SDL_video.h>

#include <infrastructure/log.hpp>
#include <rendering_engine/opengl/opengl.hpp>

void rendering_engine::opengl::context::init()
{
    LOG_INF("Init rendering_engine::OpenGL");

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        LOG_FTL("Could not initialize OpenGL (glad failed to load GL functions)");
        throw std::runtime_error{"Could not initialize OpenGL"};
    }

    int version_major = 0, version_minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &version_major);
    glGetIntegerv(GL_MINOR_VERSION, &version_minor);

    if (version_major < 3 || (version_major == 3 && version_minor < 3))
    {
        LOG_FTL("Could not initialize OpenGL, supported version is %i.%i", version_major, version_minor);
        throw std::runtime_error{"OpenGL version error! Unsupported hardware or driver"};
    }

    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* gl_glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    LOG_INF("OpenGL context: version=%i.%i", version_major, version_minor);
    LOG_INF("OpenGL vendor:   %s", gl_vendor ? gl_vendor : "<unknown>");
    LOG_INF("OpenGL renderer: %s", gl_renderer ? gl_renderer : "<unknown>");
    LOG_INF("OpenGL version:  %s", gl_version ? gl_version : "<unknown>");
    LOG_INF("GLSL version:    %s", gl_glsl ? gl_glsl : "<unknown>");
}

void rendering_engine::opengl::context::quit()
{
    LOG_INF("Quit rendering_engine::OpenGL");
}

rendering_engine::opengl::framebuffer*
rendering_engine::opengl::context::create_framebuffer(uint32_t width, uint32_t height, uint8_t color, uint8_t depth)
{
    return new rendering_engine::opengl::framebuffer(width, height, color, depth);
}

rendering_engine::opengl::program* rendering_engine::opengl::context::create_program()
{
    return new opengl::program();
}

rendering_engine::opengl::shader* rendering_engine::opengl::context::create_shader(shader_type type)
{
    return new opengl::shader(type);
}

rendering_engine::opengl::texture* rendering_engine::opengl::context::create_texture()
{
    return new opengl::texture();
}

rendering_engine::opengl::vertex_array* rendering_engine::opengl::context::create_vao()
{
    return new opengl::vertex_array();
}

rendering_engine::opengl::vertex_buffer* rendering_engine::opengl::context::create_vbo()
{
    return new opengl::vertex_buffer();
}

void rendering_engine::opengl::context::delete_framebuffer(const opengl::framebuffer* fb)
{
    delete fb;
}

void rendering_engine::opengl::context::delete_program(const opengl::program* program)
{
    delete program;
}

void rendering_engine::opengl::context::delete_shader(const opengl::shader* shader)
{
    delete shader;
}

void rendering_engine::opengl::context::delete_texture(const opengl::texture* texture)
{
    delete texture;
}

void rendering_engine::opengl::context::delete_vab(const opengl::vertex_array* vao)
{
    delete vao;
}

void rendering_engine::opengl::context::delete_vbo(const opengl::vertex_buffer* vbo)
{
    delete vbo;
}

void rendering_engine::opengl::context::enable(const rendering_engine::opengl::capability capability)
{
    glEnable((GLenum)capability);
}

void rendering_engine::opengl::context::disable(const rendering_engine::opengl::capability capability)
{
    glDisable((GLenum)capability);
}

void rendering_engine::opengl::context::clear_color(const rendering_engine::util::color& color)
{
    glClearColor(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
}

void rendering_engine::opengl::context::clear(const opengl::buffer buffers)
{
    glClear((GLbitfield)buffers);
}

void rendering_engine::opengl::context::depth_mask(const bool write_enabled)
{
    glDepthMask(write_enabled ? GL_TRUE : GL_FALSE);
}

void rendering_engine::opengl::context::bind_texture(const rendering_engine::opengl::texture& texture,
                                                     const unsigned char unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture.handle());
}

void rendering_engine::opengl::context::bind_framebuffer(const rendering_engine::opengl::framebuffer& framebuffer)
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer.handle());

    // Set viewport to frame buffer size
    GLint obj, width, height;
    glGetFramebufferAttachmentParameteriv(
        GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj);

    GLint res;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &res);
    glBindTexture(GL_TEXTURE_2D, obj);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glBindTexture(GL_TEXTURE_2D, res);

    glViewport(0, 0, width, height);
}

void rendering_engine::opengl::context::bind_framebuffer()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // Set viewport to default frame buffer size
    // glViewport(m_DefaultViewport[0], m_DefaultViewport[1], m_DefaultViewport[2], m_DefaultViewport[3]);

    /*
     * TODO: Fix glViewport for returning to default framebuffer
     */
}

void rendering_engine::opengl::context::draw_arrays(const rendering_engine::opengl::vertex_array& vao,
                                                    const rendering_engine::opengl::primitive mode,
                                                    const unsigned int offset,
                                                    const size_t vertices)
{
    glBindVertexArray(vao.handle());
    glDrawArrays((GLenum)mode, offset, (GLsizei)vertices);
    glBindVertexArray(0);
}

void rendering_engine::opengl::context::draw_elements(const rendering_engine::opengl::vertex_array& vao,
                                                      const rendering_engine::opengl::primitive mode,
                                                      const intptr_t offset,
                                                      const unsigned int count,
                                                      const rendering_engine::opengl::type type)
{
    glBindVertexArray(vao.handle());
    glDrawElements((GLenum)mode, count, (GLenum)type, (const GLvoid*)offset);
    glBindVertexArray(0);
}
