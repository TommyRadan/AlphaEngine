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

#include <infrastructure/log.hpp>
#include <rendering_engine/opengl/framebuffer.hpp>
#include <stdexcept>

#define PUSHSTATE()                                                                                                    \
    GLint restoreId;                                                                                                   \
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoreId);
#define POPSTATE() glBindFramebuffer(GL_DRAW_FRAMEBUFFER, restoreId);

rendering_engine::opengl::framebuffer::framebuffer(const uint32_t width,
                                                   const uint32_t height,
                                                   const uint8_t color,
                                                   const uint8_t depth)
    : m_object_id{0}
{
    PUSHSTATE()

    // Determine appropriate formats
    internal_format color_format;
    if (color == 24)
        color_format = internal_format::rgb;
    else if (color == 32)
        color_format = internal_format::rgba;
    else
    {
        LOG_ERR("Framebuffer could not be created, color size not supported (%u)", color);
        POPSTATE()
        return;
    }

    internal_format depth_format;
    if (depth == 8)
        depth_format = internal_format::depth_component;
    else if (depth == 16)
        depth_format = internal_format::depth_component16;
    else if (depth == 24)
        depth_format = internal_format::depth_component24;
    else if (depth == 32)
        depth_format = internal_format::depth_component32_f;
    else
    {
        LOG_ERR("Framebuffer could not be created, depth size not supported (%u)", depth);
        POPSTATE()
        return;
    }

    // Create FBO
    glGenFramebuffers(1, &m_object_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_object_id);

    // Create texture to hold color buffer
    m_color_texture.image2_d(nullptr, data_type::unsigned_byte, format::rgba, width, height, color_format);
    m_color_texture.set_wrapping_t(opengl::wrapping::clamp_edge);
    m_color_texture.set_wrapping_s(opengl::wrapping::clamp_edge);
    m_color_texture.set_filters(opengl::filter::linear, opengl::filter::linear);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_texture.handle(), 0);

    // Create renderbuffer to hold depth buffer
    if (depth > 0U)
    {
        glBindTexture(GL_TEXTURE_2D, m_depth_texture.handle());
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)depth_format, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, 0);
        m_depth_texture.set_wrapping_t(opengl::wrapping::clamp_edge);
        m_depth_texture.set_wrapping_s(opengl::wrapping::clamp_edge);
        m_depth_texture.set_filters(opengl::filter::nearest, opengl::filter::nearest);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_texture.handle(), 0);
    }

    // Check
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERR("Framebuffer could not be created, unknown reason (0x%X)",
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
        throw std::runtime_error{"Framebuffer could not be created!"};
    }

    POPSTATE()
}

namespace
{
    // Map an internal format to a (format, data_type) pair valid for a null
    // upload via glTexImage2D. Picks GL_FLOAT for floating-point internal
    // formats and unsigned_byte for normalized integer formats.
    void upload_pair_for(rendering_engine::opengl::internal_format f,
                         rendering_engine::opengl::format& out_format,
                         rendering_engine::opengl::data_type& out_type)
    {
        using rendering_engine::opengl::data_type;
        using rendering_engine::opengl::format;
        using rendering_engine::opengl::internal_format;

        switch (f)
        {
        case internal_format::rgb_a16_f:
        case internal_format::rgb_a32_f:
            out_format = format::rgba;
            out_type = data_type::Float;
            break;
        case internal_format::rg_b16_f:
        case internal_format::rg_b32_f:
            out_format = format::rgb;
            out_type = data_type::Float;
            break;
        case internal_format::r16_f:
        case internal_format::r32_f:
            out_format = format::red;
            out_type = data_type::Float;
            break;
        case internal_format::rgb:
        case internal_format::rg_b8:
        case internal_format::srg_b8:
            out_format = format::rgb;
            out_type = data_type::unsigned_byte;
            break;
        default:
            out_format = format::rgba;
            out_type = data_type::unsigned_byte;
            break;
        }
    }
} // namespace

rendering_engine::opengl::framebuffer::framebuffer(const uint32_t width,
                                                   const uint32_t height,
                                                   const internal_format color_format,
                                                   const bool with_depth)
    : m_object_id{0}
{
    PUSHSTATE()

    glGenFramebuffers(1, &m_object_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_object_id);

    format upload_format;
    data_type upload_type;
    upload_pair_for(color_format, upload_format, upload_type);

    m_color_texture.image2_d(nullptr, upload_type, upload_format, width, height, color_format);
    m_color_texture.set_wrapping_t(opengl::wrapping::clamp_edge);
    m_color_texture.set_wrapping_s(opengl::wrapping::clamp_edge);
    m_color_texture.set_filters(opengl::filter::linear, opengl::filter::linear);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_texture.handle(), 0);

    if (with_depth)
    {
        glBindTexture(GL_TEXTURE_2D, m_depth_texture.handle());
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     (GLint)internal_format::depth_component24,
                     width,
                     height,
                     0,
                     GL_DEPTH_COMPONENT,
                     GL_FLOAT,
                     0);
        m_depth_texture.set_wrapping_t(opengl::wrapping::clamp_edge);
        m_depth_texture.set_wrapping_s(opengl::wrapping::clamp_edge);
        m_depth_texture.set_filters(opengl::filter::nearest, opengl::filter::nearest);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_texture.handle(), 0);
    }

    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERR("Framebuffer could not be created (internal_format ctor): 0x%X",
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
        throw std::runtime_error{"Framebuffer could not be created!"};
    }

    POPSTATE()
}

rendering_engine::opengl::framebuffer::~framebuffer()
{
    glDeleteFramebuffers(1, &m_object_id);
}

const uint32_t rendering_engine::opengl::framebuffer::handle() const
{
    return m_object_id;
}

const rendering_engine::opengl::texture& rendering_engine::opengl::framebuffer::get_texture() const
{
    return m_color_texture;
}

const rendering_engine::opengl::texture& rendering_engine::opengl::framebuffer::get_depth_texture() const
{
    return m_depth_texture;
}
