// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file gl_device.cpp
 * @brief @c gl_device lifecycle, capabilities, swapchain, render
 *        targets, query sets, debug names, command-encoder factory
 *        and the @c lookup_* accessors. Per-resource @c gl_device
 *        member functions live in their own translation units.
 */

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <SDL3/SDL_video.h>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_check.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_command_encoder.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

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

    namespace
    {
        // The whole backend is written against this profile (direct
        // state access, immutable storage, SPIR-V shaders, compute); it
        // is also what vendor/glad was generated for.
        constexpr int required_major = 4;
        constexpr int required_minor = 6;

        // glObjectLabel rejects a label longer than GL_MAX_LABEL_LENGTH
        // (at least 256, including the terminator).
        constexpr size_t max_label_length = 255;

#if _DEBUG
        void GLAPIENTRY gl_debug_callback(GLenum source,
                                          GLenum type,
                                          GLuint id,
                                          GLenum severity,
                                          GLsizei /*length*/,
                                          const GLchar* message,
                                          const void* /*user*/)
        {
            // Notifications are driver chatter (buffer placement,
            // shader recompiles) and are dropped outright.
            if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            {
                return;
            }
            const char* source_name = gl_debug_source_name(source);
            const char* type_name = gl_debug_type_name(type);
            // API misuse and undefined behaviour are errors whatever
            // severity the driver attached; performance, portability,
            // deprecation and "other" are warnings.
            if (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR)
            {
                LOG_ERR("GL [%s/%s #%u]: %s", source_name, type_name, id, message);
            }
            else
            {
                LOG_WRN("GL [%s/%s #%u]: %s", source_name, type_name, id, message);
            }
        }
#endif

        // Bit depth of one plane of the window's default framebuffer,
        // 0 when the context was created without it.
        GLint default_framebuffer_bits(GLenum attachment, GLenum size_parameter)
        {
            GLint object_type = GL_NONE;
            glGetNamedFramebufferAttachmentParameteriv(
                0, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
            if (object_type == GL_NONE)
            {
                return 0;
            }
            GLint bits = 0;
            glGetNamedFramebufferAttachmentParameteriv(0, attachment, size_parameter, &bits);
            return bits;
        }

        GLint get_integer(GLenum parameter)
        {
            GLint value = 0;
            glGetIntegerv(parameter, &value);
            return value;
        }

        // Every power-of-two sample count up to @p max_samples, as the
        // bitmask device_limits reports.
        sample_count_mask sample_counts_up_to(GLint max_samples)
        {
            sample_count_mask mask = 0;
            for (uint32_t count = 1; static_cast<GLint>(count) <= max_samples && count != 0; count <<= 1)
            {
                mask |= count;
            }
            return mask == 0 ? 1u : mask;
        }

        void label_object(GLenum identifier, GLuint name, const char* label)
        {
            if (name == 0 || label == nullptr)
            {
                return;
            }
            const size_t length = std::min(std::strlen(label), max_label_length);
            glObjectLabel(identifier, name, static_cast<GLsizei>(length), label);
        }
    } // namespace

    void gl_device::init()
    {
        LOG_INF("Init gpu::backend::opengl::gl_device");

        const int loaded = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
        if (loaded == 0)
        {
            LOG_FTL("Could not initialize OpenGL (glad failed to load GL functions)");
            throw std::runtime_error{"Could not initialize OpenGL: the GL function loader failed"};
        }

        // glad reports the version it resolved entry points for.
        // glGetString is core since 1.0, so it is safe on any context.
        const int version_major = GLAD_VERSION_MAJOR(loaded);
        const int version_minor = GLAD_VERSION_MINOR(loaded);
        const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* gl_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* gl_glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        LOG_INF("OpenGL context: version=%i.%i", version_major, version_minor);
        LOG_INF("OpenGL vendor:   %s", gl_vendor ? gl_vendor : "<unknown>");
        LOG_INF("OpenGL renderer: %s", gl_renderer ? gl_renderer : "<unknown>");
        LOG_INF("OpenGL version:  %s", gl_version ? gl_version : "<unknown>");
        LOG_INF("GLSL version:    %s", gl_glsl ? gl_glsl : "<unknown>");

        // Gate before the first 4.x-only call below: on an older
        // context those entry points are null, and without this check
        // the first shader create would crash instead of reporting.
        if (version_major < required_major || (version_major == required_major && version_minor < required_minor))
        {
            LOG_FTL("OpenGL %i.%i is required, but the context provides %i.%i",
                    required_major,
                    required_minor,
                    version_major,
                    version_minor);
            throw std::runtime_error{"OpenGL 4.6 or newer is required, but this driver provides " +
                                     std::string{gl_version ? gl_version : "an unknown version"} + " (" +
                                     std::string{gl_renderer ? gl_renderer : "unknown renderer"} +
                                     "). Update the graphics driver or run with "
                                     "ALPHAENGINE_GRAPHICS_BACKEND=vulkan."};
        }

#if _DEBUG
        GLint context_flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &context_flags);
        if ((context_flags & GL_CONTEXT_FLAG_DEBUG_BIT) == 0)
        {
            LOG_WRN("OpenGL debug context was not granted; driver validation messages may be incomplete");
        }
        // Synchronous so a message arrives from inside the call that
        // caused it and a breakpoint in the callback lands on it.
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug_callback, nullptr);
#endif

        // Every texture upload hands over tightly packed rows, so an
        // r8 / rgb8 image with an odd width must not have its rows
        // padded to the default 4-byte alignment; readbacks and the
        // buffer <-> texture copies are tightly packed the same way.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

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

        query_capabilities();

        // Every program the backend links goes through the on-disk
        // program-binary cache when the driver and the settings allow.
        m_program_cache.init();

        // The default swapchain target is just framebuffer 0 with the
        // window's current dimensions; the engine updates dimensions
        // through @ref resize_swapchain. Its depth / stencil planes are
        // whatever the window system granted for the requested
        // 24 / 8 bits.
        const GLint depth_bits = default_framebuffer_bits(GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE);
        const GLint stencil_bits = default_framebuffer_bits(GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE);
        LOG_INF("Default framebuffer: depth=%i bits, stencil=%i bits", depth_bits, stencil_bits);
        if (depth_bits == 0)
        {
            LOG_WRN("Default framebuffer has no depth plane; passes targeting the swapchain cannot depth-test");
        }

        gl_render_target swap{};
        swap.framebuffer_id = 0;
        swap.width = 0;
        swap.height = 0;
        swap.has_depth = depth_bits > 0;
        swap.has_stencil = stencil_bits > 0;
        m_swapchain.id = m_render_targets.insert(swap);

        m_state.invalidate();
        m_initialised = true;
    }

    void gl_device::query_capabilities()
    {
        m_limits = {};
        m_limits.max_texture_size_2d = static_cast<uint32_t>(get_integer(GL_MAX_TEXTURE_SIZE));
        m_limits.max_texture_size_3d = static_cast<uint32_t>(get_integer(GL_MAX_3D_TEXTURE_SIZE));
        m_limits.max_texture_size_cube = static_cast<uint32_t>(get_integer(GL_MAX_CUBE_MAP_TEXTURE_SIZE));
        m_limits.max_array_layers = static_cast<uint32_t>(get_integer(GL_MAX_ARRAY_TEXTURE_LAYERS));
        m_limits.max_color_attachments = static_cast<uint32_t>(get_integer(GL_MAX_COLOR_ATTACHMENTS));
        m_limits.uniform_buffer_offset_alignment =
            static_cast<uint32_t>(get_integer(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT));
        m_limits.storage_buffer_offset_alignment =
            static_cast<uint32_t>(get_integer(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT));
        m_limits.color_sample_counts = sample_counts_up_to(get_integer(GL_MAX_COLOR_TEXTURE_SAMPLES));
        m_limits.depth_sample_counts = sample_counts_up_to(get_integer(GL_MAX_DEPTH_TEXTURE_SAMPLES));
        // Anisotropic filtering is core in 4.6.
        GLfloat max_anisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_anisotropy);
        m_limits.max_anisotropy = std::max(1.0f, max_anisotropy);
        // glQueryCounter(GL_TIMESTAMP) reports nanoseconds.
        m_limits.timestamp_period_ns = 1.0f;
        for (GLuint axis = 0; axis < 3; ++axis)
        {
            GLint count = 0;
            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, axis, &count);
            m_limits.max_compute_workgroup_count[axis] = static_cast<uint32_t>(count);
        }
        m_limits.max_compute_workgroup_invocations =
            static_cast<uint32_t>(get_integer(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));

        // A timestamp query with zero counter bits never resolves.
        GLint timestamp_bits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &timestamp_bits);

        // Everything below is core in the 4.6 profile init already
        // required, so the only real gates are the queried ones.
        m_features = {};
        m_features.compute = true;
        m_features.indirect_draw = true;
        m_features.multi_draw_indirect = true;
        m_features.geometry_shader = true;
        m_features.tessellation_shader = true;
        m_features.fill_mode_non_solid = true;
        m_features.sampler_anisotropy = m_limits.max_anisotropy > 1.0f;
        m_features.depth_bias_clamp = true;
        m_features.independent_blend = true;
        m_features.timestamp_queries = timestamp_bits > 0;
        m_features.debug_labels = true;
        m_features.portability_subset = false;
        // Core compute shaders, image load/store into cube-map mip
        // levels and glGenerateTextureMipmap: the IBL tables convolve
        // on the GPU.
        m_features.compute_prefilter = true;
        // ARB_gl_spirv has no push constants: a SPIR-V program declaring
        // a PushConstant block does not specialize. Pipelines' push-
        // constant ranges are ignored and the renderer keeps per-draw
        // data in uniform buffers (m_limits.max_push_constants_size
        // stays 0).
        m_features.push_constants = false;

        LOG_INF("OpenGL limits: texture %u / 3d %u / cube %u, %u array layers, %u colour attachments, "
                "anisotropy %.0f, msaa colour 0x%x depth 0x%x, timestamps %s",
                m_limits.max_texture_size_2d,
                m_limits.max_texture_size_3d,
                m_limits.max_texture_size_cube,
                m_limits.max_array_layers,
                m_limits.max_color_attachments,
                static_cast<double>(m_limits.max_anisotropy),
                m_limits.color_sample_counts,
                m_limits.depth_sample_counts,
                m_features.timestamp_queries ? "on" : "off");
    }

    texture_usage gl_device::format_support(texture_format format) const
    {
        // The block-compressed formats are sampled and uploaded, never
        // attached or stored to; BPTC and RGTC are core, S3TC and ASTC
        // extensions, so the context is asked (ARB_internalformat_query2,
        // core since 4.3) rather than assumed.
        if (is_compressed_texture_format(format))
        {
            GLint supported = GL_FALSE;
            glGetInternalformativ(GL_TEXTURE_2D,
                                  to_gl_texture_format(format).internal_format,
                                  GL_INTERNALFORMAT_SUPPORTED,
                                  1,
                                  &supported);
            return supported == GL_TRUE ? texture_usage_sampled | texture_usage_copy_dst : 0u;
        }
        // Every other engine format is a required sampled / renderable
        // internal format in 4.6. Image load/store needs a format with
        // a GLSL image layout qualifier: the sRGB and three-channel
        // formats have none, and a depth image cannot be bound.
        texture_usage usage =
            texture_usage_sampled | texture_usage_render_attachment | texture_usage_copy_src | texture_usage_copy_dst;
        if (!is_depth_texture_format(format) && format != texture_format::rgba8_srgb &&
            format != texture_format::rgb8_unorm)
        {
            usage |= texture_usage_storage;
        }
        return usage;
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
        m_samplers.for_each(
            [](gl_sampler& s)
            {
                if (s.object_id != 0)
                {
                    glDeleteSamplers(1, &s.object_id);
                    s.object_id = 0;
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
        m_query_sets.for_each(
            [](gl_query_set& q)
            {
                if (!q.ids.empty())
                {
                    glDeleteQueries(static_cast<GLsizei>(q.ids.size()), q.ids.data());
                    q.ids.clear();
                }
            });

        // The pools keep their generation counters across clear(), so
        // a handle from this lifetime never resolves in the next one.
        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();
        m_query_sets.clear();
        m_swapchain = {};

        m_program_cache.quit();
        invalidate_state_cache();
        m_initialised = false;
        LOG_INF("Quit gpu::backend::opengl::gl_device");
    }

    void gl_device::invalidate_state_cache()
    {
        m_state.invalidate();
        ++m_state_epoch;
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
        if (const char* problem = validate_render_target_descriptor(descriptor); problem != nullptr)
        {
            LOG_ERR("create_render_target: %s", problem);
            return {};
        }
        if (descriptor.color.size() > m_limits.max_color_attachments)
        {
            LOG_ERR("create_render_target: %zu colour attachments, the device allows %u",
                    descriptor.color.size(),
                    m_limits.max_color_attachments);
            return {};
        }
        const sample_count_mask sample_counts =
            descriptor.color.empty()
                ? m_limits.depth_sample_counts
                : (m_limits.color_sample_counts & (descriptor.with_depth ? m_limits.depth_sample_counts : ~0u));
        if (!sample_count_supported(sample_counts, descriptor.sample_count))
        {
            LOG_ERR("create_render_target: %u samples per pixel are not supported for these attachments",
                    descriptor.sample_count);
            return {};
        }

        gl_render_target record{};
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.samples = descriptor.sample_count;
        record.has_depth = descriptor.with_depth;

        // Textures allocated so far, released if a later step fails so
        // nothing half-built is handed out.
        std::vector<texture> allocated;
        const auto release_allocated = [&]
        {
            for (const texture t : allocated)
            {
                destroy(t);
            }
            if (record.framebuffer_id != 0)
            {
                glDeleteFramebuffers(1, &record.framebuffer_id);
            }
        };

        // Resolve one attachment: check an imported texture against the
        // target, or allocate a fresh single-mip texture of the target's
        // shape (colour attachments sample linearly, depth attachments
        // nearest, both clamped so a fullscreen pass never wraps at the
        // seam).
        const auto resolve = [&](const attachment_desc& desc, bool depth, gl_attachment& out) -> bool
        {
            if (desc.texture.valid())
            {
                const gl_texture* tex = m_textures.lookup(desc.texture.id);
                if (tex == nullptr || tex->object_id == 0)
                {
                    LOG_ERR("create_render_target: imported attachment is not a live texture");
                    return false;
                }
                if ((tex->usage & texture_usage_render_attachment) == 0u)
                {
                    LOG_ERR("create_render_target: imported texture was created without "
                            "texture_usage_render_attachment");
                    return false;
                }
                if (is_depth_texture_format(tex->format) != depth)
                {
                    LOG_ERR("create_render_target: imported texture format does not fit a %s attachment",
                            depth ? "depth" : "colour");
                    return false;
                }
                if (desc.mip_level >= tex->mip_levels || desc.layer >= tex->array_layers)
                {
                    LOG_ERR("create_render_target: level %u / layer %u is outside the imported texture (%u levels, "
                            "%u layers)",
                            desc.mip_level,
                            desc.layer,
                            tex->mip_levels,
                            tex->array_layers);
                    return false;
                }
                if (tex->samples != descriptor.sample_count)
                {
                    LOG_ERR("create_render_target: imported texture has %u samples, the target %u",
                            tex->samples,
                            descriptor.sample_count);
                    return false;
                }
                const uint32_t level_width = std::max(1u, tex->width >> desc.mip_level);
                const uint32_t level_height = std::max(1u, tex->height >> desc.mip_level);
                if (level_width != descriptor.width || level_height != descriptor.height)
                {
                    LOG_ERR("create_render_target: imported level measures %ux%u, the target %ux%u",
                            level_width,
                            level_height,
                            descriptor.width,
                            descriptor.height);
                    return false;
                }
                out.tex = desc.texture;
                out.owned = false;
                out.mip_level = desc.mip_level;
                out.layer = desc.layer;
                return true;
            }

            texture_descriptor td{};
            td.dimension = descriptor.dimension;
            td.format = desc.format;
            td.width = descriptor.width;
            td.height = descriptor.height;
            td.array_layers = descriptor.array_layers;
            td.sample_count = descriptor.sample_count;
            td.mipmaps = false;
            td.usage = texture_usage_default | texture_usage_render_attachment;
            td.min_filter = depth ? filter_mode::nearest : filter_mode::linear;
            td.mag_filter = td.min_filter;
            td.mipmap_filter = mipmap_mode::none;
            td.address_u = address_mode::clamp_edge;
            td.address_v = address_mode::clamp_edge;
            td.address_w = address_mode::clamp_edge;
            const texture t = create_texture(td);
            if (!t.valid())
            {
                return false;
            }
            allocated.push_back(t);
            out.tex = t;
            out.owned = true;
            out.mip_level = 0;
            out.layer = desc.layer;
            return true;
        };

        record.color.resize(descriptor.color.size());
        for (size_t i = 0; i < descriptor.color.size(); ++i)
        {
            if (!resolve(descriptor.color[i], false, record.color[i]))
            {
                release_allocated();
                return {};
            }
        }
        if (descriptor.with_depth)
        {
            if (!resolve(descriptor.depth, true, record.depth))
            {
                release_allocated();
                return {};
            }
            if (const gl_texture* depth_tex = m_textures.lookup(record.depth.tex.id))
            {
                record.has_stencil = has_stencil_plane(depth_tex->format);
            }
        }

        // Wired through the named entry points, so the framebuffer
        // binding the current pass (if any) may hold is untouched. A
        // layered texture (cube, array) attaches one layer; the rest
        // attach whole.
        glCreateFramebuffers(1, &record.framebuffer_id);
        const auto attach = [&](GLenum attachment_point, const gl_attachment& attachment)
        {
            const gl_texture* tex = m_textures.lookup(attachment.tex.id);
            if (tex == nullptr)
            {
                return;
            }
            if (tex->layered)
            {
                GL_CHECK(glNamedFramebufferTextureLayer(record.framebuffer_id,
                                                        attachment_point,
                                                        tex->object_id,
                                                        static_cast<GLint>(attachment.mip_level),
                                                        static_cast<GLint>(attachment.layer)));
            }
            else
            {
                GL_CHECK(glNamedFramebufferTexture(
                    record.framebuffer_id, attachment_point, tex->object_id, static_cast<GLint>(attachment.mip_level)));
            }
        };
        std::array<GLenum, max_color_attachments> draw_buffers{};
        for (size_t i = 0; i < record.color.size(); ++i)
        {
            draw_buffers[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
            attach(draw_buffers[i], record.color[i]);
        }
        if (record.color.empty())
        {
            // A depth-only framebuffer is complete once it draws to and
            // reads from no colour buffer.
            glNamedFramebufferDrawBuffer(record.framebuffer_id, GL_NONE);
            glNamedFramebufferReadBuffer(record.framebuffer_id, GL_NONE);
        }
        else
        {
            GL_CHECK(glNamedFramebufferDrawBuffers(
                record.framebuffer_id, static_cast<GLsizei>(record.color.size()), draw_buffers.data()));
        }
        if (record.has_depth)
        {
            // A packed depth-stencil texture must go on the combined
            // attachment point; GL_DEPTH_ATTACHMENT alone leaves the
            // stencil plane unattached.
            attach(record.has_stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, record.depth);
        }

        const GLenum status = glCheckNamedFramebufferStatus(record.framebuffer_id, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG_ERR("create_render_target: incomplete framebuffer (status 0x%x)", status);
            release_allocated();
            return {};
        }

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
            // Only the attachments the target allocated go with it; an
            // imported texture stays with its owner.
            for (gl_attachment& attachment : record->color)
            {
                if (attachment.owned && attachment.tex.valid())
                {
                    destroy(attachment.tex);
                }
                attachment = {};
            }
            if (record->depth.owned && record->depth.tex.valid())
            {
                destroy(record->depth.tex);
            }
            record->depth = {};
            m_render_targets.remove(handle.id);
            invalidate_state_cache();
        }
    }

    texture gl_device::render_target_color_texture(render_target handle, uint32_t index)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            if (index < record->color.size())
            {
                return record->color[index].tex;
            }
        }
        return {};
    }

    texture gl_device::render_target_depth_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->depth.tex;
        }
        return {};
    }

    // -- Queries ------------------------------------------------------

    query_set gl_device::create_query_set(const query_set_descriptor& descriptor)
    {
        if (!m_features.timestamp_queries)
        {
            LOG_WRN("create_query_set: the context has no timestamp counter; no query set created");
            return {};
        }
        if (descriptor.count == 0)
        {
            LOG_ERR("create_query_set: a query set needs at least one query");
            return {};
        }
        gl_query_set record{};
        record.ids.resize(descriptor.count, 0);
        GL_CHECK(glCreateQueries(GL_TIMESTAMP, static_cast<GLsizei>(descriptor.count), record.ids.data()));
        query_set h{};
        h.id = m_query_sets.insert(record);
        return h;
    }

    void gl_device::destroy(query_set handle)
    {
        if (auto* record = m_query_sets.lookup(handle.id))
        {
            if (!record->ids.empty())
            {
                glDeleteQueries(static_cast<GLsizei>(record->ids.size()), record->ids.data());
            }
            m_query_sets.remove(handle.id);
        }
    }

    bool gl_device::resolve_queries(query_set set, uint32_t first, uint32_t count, uint64_t* out_ticks)
    {
        auto* record = m_query_sets.lookup(set.id);
        if (record == nullptr || out_ticks == nullptr)
        {
            return false;
        }
        if (first > record->ids.size() || count > record->ids.size() - first)
        {
            LOG_WRN("resolve_queries: %u queries from %u exceed the %zu-query set", count, first, record->ids.size());
            return false;
        }
        // All or nothing: a caller that mixes this frame's and last
        // frame's values would report nonsense intervals.
        for (uint32_t i = 0; i < count; ++i)
        {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(record->ids[first + i], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_FALSE)
            {
                return false;
            }
        }
        for (uint32_t i = 0; i < count; ++i)
        {
            GLuint64 ticks = 0;
            glGetQueryObjectui64v(record->ids[first + i], GL_QUERY_RESULT, &ticks);
            out_ticks[i] = ticks;
        }
        return true;
    }

    // -- Debug names --------------------------------------------------

    void gl_device::set_debug_name(buffer handle, const char* name)
    {
        if (const auto* record = m_buffers.lookup(handle.id))
        {
            label_object(GL_BUFFER, record->object_id, name);
        }
    }

    void gl_device::set_debug_name(texture handle, const char* name)
    {
        if (const auto* record = m_textures.lookup(handle.id))
        {
            label_object(GL_TEXTURE, record->object_id, name);
        }
    }

    void gl_device::set_debug_name(sampler handle, const char* name)
    {
        if (const auto* record = m_samplers.lookup(handle.id))
        {
            label_object(GL_SAMPLER, record->object_id, name);
        }
    }

    void gl_device::set_debug_name(pipeline handle, const char* name)
    {
        if (const auto* record = m_pipelines.lookup(handle.id))
        {
            label_object(GL_PROGRAM, record->program_id, name);
        }
    }

    void gl_device::set_debug_name(render_target handle, const char* name)
    {
        // Framebuffer 0 (the swapchain) cannot be labelled.
        if (const auto* record = m_render_targets.lookup(handle.id))
        {
            label_object(GL_FRAMEBUFFER, record->framebuffer_id, name);
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

    void gl_device::begin_frame()
    {
        // Every recorded command has executed by the time the next
        // frame starts and destroy() releases GL objects immediately,
        // so there is nothing to wait for or drain.
    }

    void gl_device::end_frame()
    {
        // Presentation stays with window::swap_buffers on this backend.
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

    gl_query_set* gl_device::lookup_query_set(query_set h)
    {
        return m_query_sets.lookup(h.id);
    }
} // namespace rendering_engine::gpu::backend::opengl
