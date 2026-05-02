/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

/**
 * @file gl_device.cpp
 * @brief @c gl_device lifecycle, swapchain, command-encoder, and the
 *        @c lookup_* accessors. Per-resource @c gl_device member
 *        functions live in their own translation units.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <stdexcept>

#include <glad/gl.h>
#include <SDL3/SDL_video.h>

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_command_encoder.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    gl_device::gl_device() = default;

    gl_device::~gl_device()
    {
        if (m_initialised)
        {
            quit();
        }
    }

    void gl_device::init()
    {
        LOG_INF("Init gpu::backend::opengl::gl_device");

        if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
        {
            LOG_FTL("Could not initialize OpenGL (glad failed to load GL functions)");
            throw std::runtime_error{"Could not initialize OpenGL"};
        }

        int version_major = 0;
        int version_minor = 0;
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

        // The default swapchain target is just framebuffer
        // 0 with the window's current dimensions; the engine
        // updates dimensions through @ref resize_swapchain.
        gl_render_target swap{};
        swap.framebuffer_id = 0;
        swap.width = 0;
        swap.height = 0;
        swap.has_depth = true;
        m_swapchain.id = m_render_targets.insert(swap);

        m_initialised = true;
    }

    void gl_device::quit()
    {
        if (!m_initialised)
        {
            return;
        }

        // Release any GL objects still alive in the pools.
        // Resources the user explicitly destroyed are
        // already gone; this catches leaks at shutdown.
        m_pipelines.for_each(
            [](gl_pipeline& p)
            {
                if (p.program_id != 0)
                {
                    glDeleteProgram(p.program_id);
                    p.program_id = 0;
                }
                if (p.vao_id != 0)
                {
                    glDeleteVertexArrays(1, &p.vao_id);
                    p.vao_id = 0;
                }
            });
        m_shader_modules.for_each(
            [](gl_shader_module& s)
            {
                if (s.object_id != 0)
                {
                    glDeleteShader(s.object_id);
                    s.object_id = 0;
                }
            });
        m_textures.for_each(
            [](gl_texture& t)
            {
                if (t.object_id != 0)
                {
                    glDeleteTextures(1, &t.object_id);
                    t.object_id = 0;
                }
            });
        m_buffers.for_each(
            [](gl_buffer& b)
            {
                if (b.object_id != 0)
                {
                    glDeleteBuffers(1, &b.object_id);
                    b.object_id = 0;
                }
            });

        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();

        m_initialised = false;
        LOG_INF("Quit gpu::backend::opengl::gl_device");
    }

    // -- Render targets / swapchain -------------------------------

    render_target gl_device::swapchain_target()
    {
        return m_swapchain;
    }

    void gl_device::resize_swapchain(uint32_t width, uint32_t height)
    {
        if (auto* record = m_render_targets.lookup(m_swapchain.id))
        {
            record->width = width;
            record->height = height;
        }
    }

    // -- Command recording ----------------------------------------

    std::unique_ptr<command_encoder> gl_device::create_command_encoder()
    {
        return std::make_unique<gl_command_encoder>(*this);
    }

    void gl_device::submit(std::unique_ptr<command_encoder> encoder)
    {
        // OpenGL has no deferred submission — the encoder's
        // recorded operations have already executed by the
        // time the caller calls @c submit. Releasing the
        // unique_ptr here is the entire body.
        encoder.reset();
    }

    // -- Internal accessors ---------------------------------------

    gl_buffer* gl_device::lookup_buffer(buffer h)
    {
        return m_buffers.lookup(h.id);
    }

    gl_texture* gl_device::lookup_texture(texture h)
    {
        return m_textures.lookup(h.id);
    }

    gl_sampler* gl_device::lookup_sampler(sampler h)
    {
        return m_samplers.lookup(h.id);
    }

    gl_pipeline* gl_device::lookup_pipeline(pipeline h)
    {
        return m_pipelines.lookup(h.id);
    }

    gl_bind_group* gl_device::lookup_bind_group(bind_group h)
    {
        return m_bind_groups.lookup(h.id);
    }

    gl_render_target* gl_device::lookup_render_target(render_target h)
    {
        return m_render_targets.lookup(h.id);
    }

    gl_bind_group_layout* gl_device::lookup_bind_group_layout(bind_group_layout h)
    {
        return m_bind_group_layouts.lookup(h.id);
    }
} // namespace rendering_engine::gpu::backend::opengl
