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

#include <rendering_engine/gpu/backend/opengl/gl_device.hpp>

#include <stdexcept>
#include <string>
#include <utility>

#include <SDL3/SDL_video.h>

#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_command_encoder.hpp>
#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        namespace backend
        {
            namespace opengl
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
                        LOG_FTL(
                            "Could not initialize OpenGL, supported version is %i.%i", version_major, version_minor);
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

                // -- Buffers --------------------------------------------------

                buffer gl_device::create_buffer(const buffer_descriptor& descriptor)
                {
                    gl_buffer record{};
                    record.size = descriptor.size;
                    record.usage = descriptor.usage;
                    if ((descriptor.usage & buffer_usage_index) != 0u)
                    {
                        record.default_target = GL_ELEMENT_ARRAY_BUFFER;
                    }
                    else if ((descriptor.usage & buffer_usage_uniform) != 0u)
                    {
                        record.default_target = GL_UNIFORM_BUFFER;
                    }
                    else
                    {
                        record.default_target = GL_ARRAY_BUFFER;
                    }

                    glGenBuffers(1, &record.object_id);
                    glBindBuffer(record.default_target, record.object_id);
                    glBufferData(record.default_target,
                                 static_cast<GLsizeiptr>(descriptor.size),
                                 descriptor.initial_data,
                                 to_gl_buffer_usage_hint(descriptor.hint));
                    glBindBuffer(record.default_target, 0);

                    buffer h{};
                    h.id = m_buffers.insert(record);
                    return h;
                }

                void gl_device::destroy(buffer handle)
                {
                    if (auto* record = m_buffers.lookup(handle.id))
                    {
                        if (record->object_id != 0)
                        {
                            glDeleteBuffers(1, &record->object_id);
                        }
                        m_buffers.remove(handle.id);
                    }
                }

                void gl_device::write_buffer(buffer handle, const void* data, size_t size, size_t offset)
                {
                    auto* record = m_buffers.lookup(handle.id);
                    if (record == nullptr || record->object_id == 0)
                    {
                        LOG_WRN("write_buffer: invalid buffer handle");
                        return;
                    }
                    glBindBuffer(record->default_target, record->object_id);
                    glBufferSubData(
                        record->default_target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
                    glBindBuffer(record->default_target, 0);
                }

                // -- Textures -------------------------------------------------

                namespace
                {
                    void apply_sampler_state(GLenum target,
                                             const filter_mode min,
                                             const filter_mode mag,
                                             const mipmap_mode mip,
                                             const address_mode u,
                                             const address_mode v,
                                             const address_mode w)
                    {
                        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, to_gl_min_filter(min, mip));
                        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, to_gl_mag_filter(mag));
                        glTexParameteri(target, GL_TEXTURE_WRAP_S, to_gl_address_mode(u));
                        glTexParameteri(target, GL_TEXTURE_WRAP_T, to_gl_address_mode(v));
                        glTexParameteri(target, GL_TEXTURE_WRAP_R, to_gl_address_mode(w));
                    }
                } // namespace

                texture gl_device::create_texture(const texture_descriptor& descriptor)
                {
                    gl_texture record{};
                    record.target =
                        descriptor.dimension == texture_dimension::cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
                    record.format = descriptor.format;
                    record.width = descriptor.width;
                    record.height = descriptor.height;
                    record.mipmaps = descriptor.mipmaps;

                    glGenTextures(1, &record.object_id);
                    glBindTexture(record.target, record.object_id);

                    apply_sampler_state(record.target,
                                        descriptor.min_filter,
                                        descriptor.mag_filter,
                                        descriptor.mipmap_filter,
                                        descriptor.address_u,
                                        descriptor.address_v,
                                        descriptor.address_w);

                    // Allocate storage. For 2D textures we allocate
                    // mip-0 directly via @c glTexImage2D with a null
                    // pointer; the caller fills it via
                    // @ref write_texture or @ref write_cube_face. For
                    // cube maps we allocate all six faces with null data
                    // so each face can be uploaded independently later.
                    const auto fmt = to_gl_texture_format(descriptor.format);
                    if (record.target == GL_TEXTURE_2D)
                    {
                        glTexImage2D(GL_TEXTURE_2D,
                                     0,
                                     fmt.internal_format,
                                     static_cast<GLsizei>(descriptor.width),
                                     static_cast<GLsizei>(descriptor.height),
                                     0,
                                     fmt.upload_format,
                                     fmt.upload_type,
                                     nullptr);
                    }
                    else
                    {
                        for (int face = 0; face < 6; ++face)
                        {
                            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                         0,
                                         fmt.internal_format,
                                         static_cast<GLsizei>(descriptor.width),
                                         static_cast<GLsizei>(descriptor.height),
                                         0,
                                         fmt.upload_format,
                                         fmt.upload_type,
                                         nullptr);
                        }
                    }

                    glBindTexture(record.target, 0);

                    texture h{};
                    h.id = m_textures.insert(record);
                    return h;
                }

                void gl_device::destroy(texture handle)
                {
                    if (auto* record = m_textures.lookup(handle.id))
                    {
                        if (record->object_id != 0)
                        {
                            glDeleteTextures(1, &record->object_id);
                        }
                        m_textures.remove(handle.id);
                    }
                }

                void gl_device::write_texture(texture handle, const void* data, size_t /*size*/)
                {
                    auto* record = m_textures.lookup(handle.id);
                    if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_2D)
                    {
                        LOG_WRN("write_texture: invalid 2D texture handle");
                        return;
                    }
                    const auto fmt = to_gl_texture_format(record->format);
                    glBindTexture(GL_TEXTURE_2D, record->object_id);
                    glTexSubImage2D(GL_TEXTURE_2D,
                                    0,
                                    0,
                                    0,
                                    static_cast<GLsizei>(record->width),
                                    static_cast<GLsizei>(record->height),
                                    fmt.upload_format,
                                    fmt.upload_type,
                                    data);
                    glBindTexture(GL_TEXTURE_2D, 0);
                }

                void gl_device::write_cube_face(texture handle, cube_face face, const void* data, size_t /*size*/)
                {
                    auto* record = m_textures.lookup(handle.id);
                    if (record == nullptr || record->object_id == 0 || record->target != GL_TEXTURE_CUBE_MAP)
                    {
                        LOG_WRN("write_cube_face: invalid cube texture handle");
                        return;
                    }
                    const auto fmt = to_gl_texture_format(record->format);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, record->object_id);
                    glTexSubImage2D(to_gl_cube_face(face),
                                    0,
                                    0,
                                    0,
                                    static_cast<GLsizei>(record->width),
                                    static_cast<GLsizei>(record->height),
                                    fmt.upload_format,
                                    fmt.upload_type,
                                    data);
                    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                }

                void gl_device::generate_mipmaps(texture handle)
                {
                    auto* record = m_textures.lookup(handle.id);
                    if (record == nullptr || record->object_id == 0)
                    {
                        return;
                    }
                    glBindTexture(record->target, record->object_id);
                    glGenerateMipmap(record->target);
                    glBindTexture(record->target, 0);
                }

                // -- Samplers -------------------------------------------------

                sampler gl_device::create_sampler(const sampler_descriptor& descriptor)
                {
                    // The GL backend currently expresses sampler state on
                    // the texture itself (set at create time via
                    // @c texture_descriptor). Stand-alone samplers exist
                    // in the API for forward-compatibility with explicit
                    // binding-model backends; on GL they are recorded
                    // here and treated as no-ops at bind time.
                    gl_sampler record{};
                    record.descriptor = descriptor;
                    sampler h{};
                    h.id = m_samplers.insert(record);
                    return h;
                }

                void gl_device::destroy(sampler handle)
                {
                    m_samplers.remove(handle.id);
                }

                // -- Shader modules -------------------------------------------

                shader_module gl_device::create_shader_module(const shader_module_descriptor& descriptor)
                {
                    gl_shader_module record{};
                    record.stage = descriptor.stage;
                    record.object_id = glCreateShader(to_gl_shader_stage(descriptor.stage));
                    if (record.object_id == 0)
                    {
                        LOG_FTL("create_shader_module: glCreateShader returned 0");
                        throw std::runtime_error{"shader creation failed"};
                    }

                    const char* source = descriptor.source.c_str();
                    const auto length = static_cast<GLint>(descriptor.source.length());
                    glShaderSource(record.object_id, 1, &source, &length);
                    glCompileShader(record.object_id);

                    GLint compiled = 0;
                    glGetShaderiv(record.object_id, GL_COMPILE_STATUS, &compiled);
                    if (compiled != GL_TRUE)
                    {
                        GLint log_length = 0;
                        glGetShaderiv(record.object_id, GL_INFO_LOG_LENGTH, &log_length);
                        std::string info_log(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
                        if (log_length > 0)
                        {
                            glGetShaderInfoLog(record.object_id, log_length, nullptr, info_log.data());
                        }
                        LOG_ERR("Shader compile failed: %s", info_log.c_str());
                        glDeleteShader(record.object_id);
                        throw std::runtime_error{info_log};
                    }

                    LOG_INF("Compiled shader id=%u stage=%i", record.object_id, static_cast<int>(descriptor.stage));

                    shader_module h{};
                    h.id = m_shader_modules.insert(record);
                    return h;
                }

                void gl_device::destroy(shader_module handle)
                {
                    if (auto* record = m_shader_modules.lookup(handle.id))
                    {
                        if (record->object_id != 0)
                        {
                            glDeleteShader(record->object_id);
                        }
                        m_shader_modules.remove(handle.id);
                    }
                }

                // -- Bind group layouts ---------------------------------------

                bind_group_layout gl_device::create_bind_group_layout(const bind_group_layout_descriptor& descriptor)
                {
                    gl_bind_group_layout record{};
                    record.descriptor = descriptor;
                    bind_group_layout h{};
                    h.id = m_bind_group_layouts.insert(record);
                    return h;
                }

                void gl_device::destroy(bind_group_layout handle)
                {
                    m_bind_group_layouts.remove(handle.id);
                }

                // -- Pipelines ------------------------------------------------

                pipeline gl_device::create_pipeline(const pipeline_descriptor& descriptor)
                {
                    gl_pipeline record{};
                    record.topology = descriptor.topology;
                    record.blend = descriptor.blend;
                    record.depth = descriptor.depth;
                    record.rasterizer = descriptor.rasterizer;
                    record.vertex_buffers = descriptor.vertex_buffers;
                    record.bind_group_layouts = descriptor.bind_group_layouts;

                    record.program_id = glCreateProgram();
                    if (record.program_id == 0)
                    {
                        LOG_FTL("create_pipeline: glCreateProgram returned 0");
                        throw std::runtime_error{"program creation failed"};
                    }

                    const auto attach = [&](shader_module module)
                    {
                        if (!module.valid())
                        {
                            return;
                        }
                        auto* shader_record = m_shader_modules.lookup(module.id);
                        if (shader_record != nullptr && shader_record->object_id != 0)
                        {
                            glAttachShader(record.program_id, shader_record->object_id);
                        }
                    };
                    attach(descriptor.vertex_shader);
                    attach(descriptor.fragment_shader);
                    attach(descriptor.geometry_shader);

                    glLinkProgram(record.program_id);
                    GLint linked = 0;
                    glGetProgramiv(record.program_id, GL_LINK_STATUS, &linked);
                    if (linked != GL_TRUE)
                    {
                        GLint log_length = 0;
                        glGetProgramiv(record.program_id, GL_INFO_LOG_LENGTH, &log_length);
                        std::string info_log(log_length > 0 ? static_cast<size_t>(log_length) : 1u, '\0');
                        if (log_length > 0)
                        {
                            glGetProgramInfoLog(record.program_id, log_length, nullptr, info_log.data());
                        }
                        LOG_ERR("Program link failed: %s", info_log.c_str());
                        glDeleteProgram(record.program_id);
                        throw std::runtime_error{info_log};
                    }

                    // Build a VAO that owns the vertex format declared by
                    // the pipeline. Vertex buffers bound at draw time
                    // assume this VAO is bound; the encoder rebinds
                    // @c GL_ARRAY_BUFFER and re-issues
                    // @c glVertexAttribPointer for the slot's attributes.
                    glGenVertexArrays(1, &record.vao_id);
                    glBindVertexArray(record.vao_id);
                    for (const auto& layout : descriptor.vertex_buffers)
                    {
                        for (const auto& attribute : layout.attributes)
                        {
                            glEnableVertexAttribArray(attribute.location);
                        }
                    }
                    glBindVertexArray(0);

                    // Resolve and cache uniform locations / texture-unit
                    // ordinals for every bind-group layout entry. After
                    // this, set_bind_group is a flat array lookup.
                    record.cached_locations.resize(descriptor.bind_group_layouts.size());
                    int next_texture_unit = 0;
                    for (size_t group_index = 0; group_index < descriptor.bind_group_layouts.size(); ++group_index)
                    {
                        const auto layout_handle = descriptor.bind_group_layouts[group_index];
                        const auto* layout = m_bind_group_layouts.lookup(layout_handle.id);
                        if (layout == nullptr)
                        {
                            continue;
                        }
                        auto& slots = record.cached_locations[group_index];
                        slots.resize(layout->descriptor.entries.size(), -1);

                        for (size_t entry_index = 0; entry_index < layout->descriptor.entries.size(); ++entry_index)
                        {
                            const auto& entry = layout->descriptor.entries[entry_index];
                            if (entry.kind == binding_kind::texture)
                            {
                                // Cache: high 16 bits = uniform location of
                                // the sampler uniform, low 16 bits = texture
                                // unit ordinal. Assigned in declaration
                                // order across all groups so the assignment
                                // is deterministic per pipeline.
                                const GLint uniform_loc = glGetUniformLocation(record.program_id, entry.name.c_str());
                                const int unit = next_texture_unit++;
                                slots[entry_index] = (uniform_loc << 8) | (unit & 0xFF);
                            }
                            else if (entry.kind == binding_kind::sampler)
                            {
                                slots[entry_index] = -1; // ignored on the GL backend
                            }
                            else
                            {
                                slots[entry_index] = glGetUniformLocation(record.program_id, entry.name.c_str());
                            }
                        }
                    }

                    LOG_INF("Pipeline linked id=%u vao=%u", record.program_id, record.vao_id);

                    pipeline h{};
                    h.id = m_pipelines.insert(record);
                    return h;
                }

                void gl_device::destroy(pipeline handle)
                {
                    if (auto* record = m_pipelines.lookup(handle.id))
                    {
                        if (record->program_id != 0)
                        {
                            glDeleteProgram(record->program_id);
                        }
                        if (record->vao_id != 0)
                        {
                            glDeleteVertexArrays(1, &record->vao_id);
                        }
                        m_pipelines.remove(handle.id);
                    }
                }

                // -- Bind groups ----------------------------------------------

                bind_group gl_device::create_bind_group(const bind_group_descriptor& descriptor)
                {
                    gl_bind_group record{};
                    record.layout = descriptor.layout;
                    record.entries = descriptor.entries;
                    bind_group h{};
                    h.id = m_bind_groups.insert(record);
                    return h;
                }

                void gl_device::destroy(bind_group handle)
                {
                    m_bind_groups.remove(handle.id);
                }

                void gl_device::update_bind_group(bind_group handle, const std::vector<binding_value>& entries)
                {
                    if (auto* record = m_bind_groups.lookup(handle.id))
                    {
                        record->entries = entries;
                    }
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
            } // namespace opengl
        } // namespace backend
    } // namespace gpu
} // namespace rendering_engine
