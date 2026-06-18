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

#include <core/log.hpp>
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

#if _DEBUG
    static void GLAPIENTRY gl_debug_callback(GLenum /*source*/,
                                             GLenum type,
                                             GLuint /*id*/,
                                             GLenum severity,
                                             GLsizei /*length*/,
                                             const GLchar* message,
                                             const void* /*user*/)
    {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        {
            return;
        }
        if (type == GL_DEBUG_TYPE_ERROR)
        {
            LOG_ERR("GL: %s", message);
        }
        else
        {
            LOG_WRN("GL: %s", message);
        }
    }
#endif

    void gl_device::init()
    {
        LOG_INF("Init gpu::backend::opengl::gl_device");

        if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
        {
            LOG_FTL("Could not initialize OpenGL (glad failed to load GL functions)");
            throw std::runtime_error{"Could not initialize OpenGL"};
        }

#if _DEBUG
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug_callback, nullptr);
#endif

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

        // Filter cube-map samples across face boundaries instead of
        // clamping within each face. Without this, sampling near a face
        // edge (and every mip of a prefiltered IBL cube) shows hard seams,
        // and a moving camera makes a mirror reflection pop as the
        // reflection vector crosses them. Core since OpenGL 3.2.
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        // Let the vertex shader drive @c gl_PointSize for point-list
        // pipelines (e.g. @ref points_material). Without this the
        // fixed-function point size is locked to whatever the last
        // @c glPointSize set, so per-material sizing would be ignored.
        // Core since OpenGL 3.2; on Vulkan @c gl_PointSize is always
        // honoured, so this keeps both backends in agreement.
        glEnable(GL_PROGRAM_POINT_SIZE);

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
        m_render_targets.for_each(
            [](gl_render_target& rt)
            {
                if (rt.framebuffer_id != 0)
                {
                    glDeleteFramebuffers(1, &rt.framebuffer_id);
                    rt.framebuffer_id = 0;
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

    render_target gl_device::create_render_target(const render_target_descriptor& descriptor)
    {
        // Allocate the colour attachment as a regular sampled texture
        // so post passes can read it as input. clamp_edge keeps the
        // fullscreen pass from wrapping at the seam.
        texture_descriptor color_descriptor{};
        color_descriptor.dimension = texture_dimension::d2;
        color_descriptor.format = descriptor.color_format;
        color_descriptor.width = descriptor.width;
        color_descriptor.height = descriptor.height;
        color_descriptor.mipmaps = false;
        color_descriptor.min_filter = filter_mode::linear;
        color_descriptor.mag_filter = filter_mode::linear;
        color_descriptor.mipmap_filter = mipmap_mode::none;
        color_descriptor.address_u = address_mode::clamp_edge;
        color_descriptor.address_v = address_mode::clamp_edge;
        color_descriptor.address_w = address_mode::clamp_edge;
        const texture color = create_texture(color_descriptor);

        texture depth{};
        if (descriptor.with_depth)
        {
            texture_descriptor depth_descriptor{};
            depth_descriptor.dimension = texture_dimension::d2;
            depth_descriptor.format = descriptor.depth_format;
            depth_descriptor.width = descriptor.width;
            depth_descriptor.height = descriptor.height;
            depth_descriptor.mipmaps = false;
            depth_descriptor.min_filter = filter_mode::nearest;
            depth_descriptor.mag_filter = filter_mode::nearest;
            depth_descriptor.mipmap_filter = mipmap_mode::none;
            depth_descriptor.address_u = address_mode::clamp_edge;
            depth_descriptor.address_v = address_mode::clamp_edge;
            depth_descriptor.address_w = address_mode::clamp_edge;
            depth = create_texture(depth_descriptor);
        }

        gl_render_target record{};
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.has_depth = descriptor.with_depth;
        record.color_attachment = color;
        record.depth_attachment = depth;

        glGenFramebuffers(1, &record.framebuffer_id);
        glBindFramebuffer(GL_FRAMEBUFFER, record.framebuffer_id);

        if (auto* color_record = m_textures.lookup(color.id))
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_record->object_id, 0);
        }
        if (descriptor.with_depth)
        {
            if (auto* depth_record = m_textures.lookup(depth.id))
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_record->object_id, 0);
            }
        }

        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG_ERR("create_render_target: incomplete framebuffer (status 0x%x)", status);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        render_target h{};
        h.id = m_render_targets.insert(record);
        return h;
    }

    void gl_device::destroy(render_target handle)
    {
        // The swapchain is owned by the device and survives until quit.
        if (handle.id == m_swapchain.id)
        {
            return;
        }
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            if (record->framebuffer_id != 0)
            {
                glDeleteFramebuffers(1, &record->framebuffer_id);
                record->framebuffer_id = 0;
            }
            if (record->color_attachment.valid())
            {
                destroy(record->color_attachment);
                record->color_attachment = {};
            }
            if (record->depth_attachment.valid())
            {
                destroy(record->depth_attachment);
                record->depth_attachment = {};
            }
            m_render_targets.remove(handle.id);
        }
    }

    texture gl_device::render_target_color_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->color_attachment;
        }
        return {};
    }

    texture gl_device::render_target_depth_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->depth_attachment;
        }
        return {};
    }

    // -- Command recording ----------------------------------------

    std::unique_ptr<command_encoder> gl_device::create_command_encoder(uint32_t /*recording_context*/)
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
