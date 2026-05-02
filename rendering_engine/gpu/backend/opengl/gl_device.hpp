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
 * @file gl_device.hpp
 * @brief OpenGL 3.3 implementation of @ref gpu::device.
 *
 * Owns one resource pool per handle type, plus the per-handle generation
 * counter that distinguishes a recycled slot from a stale handle. Every
 * GL call in the engine ultimately funnels through this translation
 * unit (and the small @c gl_translate helpers).
 */

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glad/gl.h>

#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        namespace backend
        {
            namespace opengl
            {
                // -- GL-side resource records ---------------------------------

                struct gl_buffer
                {
                    GLuint object_id{0};
                    GLenum default_target{0}; // GL_ARRAY_BUFFER / _ELEMENT_ARRAY_BUFFER / _UNIFORM_BUFFER
                    size_t size{0};
                    buffer_usage usage{0};
                };

                struct gl_texture
                {
                    GLuint object_id{0};
                    GLenum target{0}; // GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP
                    texture_format format{texture_format::rgba8_unorm};
                    uint32_t width{0};
                    uint32_t height{0};
                    bool mipmaps{false};
                };

                struct gl_sampler
                {
                    // Stored sampler parameters; applied to the bound
                    // texture at draw time. GL 3.3 doesn't require a
                    // sampler object, so the backend just remembers the
                    // settings here and reapplies via @c glTexParameteri.
                    sampler_descriptor descriptor;
                };

                struct gl_shader_module
                {
                    GLuint object_id{0};
                    shader_stage stage{shader_stage::vertex};
                };

                struct gl_bind_group_layout
                {
                    bind_group_layout_descriptor descriptor;
                };

                struct gl_pipeline
                {
                    GLuint program_id{0};
                    GLuint vao_id{0};

                    primitive_topology topology{primitive_topology::triangles};
                    blend_state blend;
                    depth_state depth;
                    rasterizer_state rasterizer;

                    std::vector<vertex_buffer_layout> vertex_buffers;
                    std::vector<bind_group_layout> bind_group_layouts;

                    // Cached per-bind-group, per-binding uniform / texture-unit
                    // assignment, indexed as cached_locations[group][slot_index].
                    // For texture / sampler entries the value is a texture
                    // unit ordinal; for value entries it is the GL uniform
                    // location returned by @c glGetUniformLocation.
                    std::vector<std::vector<GLint>> cached_locations;
                };

                struct gl_bind_group
                {
                    bind_group_layout layout{};
                    std::vector<binding_value> entries;
                };

                struct gl_render_target
                {
                    GLuint framebuffer_id{0}; // 0 == default swapchain
                    uint32_t width{0};
                    uint32_t height{0};
                    bool has_depth{true};
                };

                // -- Resource pool ------------------------------------------------
                //
                // Generic indexed slot allocator with a per-slot generation
                // counter. The encoded handle is @c (generation << 32 |
                // slot_index + 1) — the +1 reserves zero as the "invalid"
                // marker so a default-constructed handle never resolves.

                template<typename T>
                class gl_pool
                {
                public:
                    uint64_t insert(T value)
                    {
                        uint32_t slot = 0;
                        if (!m_free.empty())
                        {
                            slot = m_free.back();
                            m_free.pop_back();
                            m_slots[slot] = std::move(value);
                            m_alive[slot] = true;
                        }
                        else
                        {
                            slot = static_cast<uint32_t>(m_slots.size());
                            m_slots.push_back(std::move(value));
                            m_generations.push_back(1);
                            m_alive.push_back(true);
                        }
                        const uint32_t gen = m_generations[slot];
                        return encode(slot, gen);
                    }

                    T* lookup(uint64_t encoded)
                    {
                        uint32_t slot = 0, gen = 0;
                        decode(encoded, slot, gen);
                        if (slot >= m_slots.size() || !m_alive[slot] || m_generations[slot] != gen)
                        {
                            return nullptr;
                        }
                        return &m_slots[slot];
                    }

                    bool remove(uint64_t encoded)
                    {
                        uint32_t slot = 0, gen = 0;
                        decode(encoded, slot, gen);
                        if (slot >= m_slots.size() || !m_alive[slot] || m_generations[slot] != gen)
                        {
                            return false;
                        }
                        m_alive[slot] = false;
                        m_generations[slot]++;
                        m_free.push_back(slot);
                        return true;
                    }

                    // Iterate live entries. Used during quit() to release
                    // any GL objects that callers leaked.
                    template<typename Fn>
                    void for_each(Fn&& fn)
                    {
                        for (size_t i = 0; i < m_slots.size(); ++i)
                        {
                            if (m_alive[i])
                            {
                                fn(m_slots[i]);
                            }
                        }
                    }

                    void clear()
                    {
                        m_slots.clear();
                        m_generations.clear();
                        m_alive.clear();
                        m_free.clear();
                    }

                private:
                    static uint64_t encode(uint32_t slot, uint32_t gen)
                    {
                        return (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(slot + 1);
                    }
                    static void decode(uint64_t encoded, uint32_t& slot, uint32_t& gen)
                    {
                        if (encoded == 0)
                        {
                            slot = static_cast<uint32_t>(-1);
                            gen = 0;
                            return;
                        }
                        slot = static_cast<uint32_t>(encoded & 0xFFFFFFFFu) - 1;
                        gen = static_cast<uint32_t>(encoded >> 32);
                    }

                    std::vector<T> m_slots;
                    std::vector<uint32_t> m_generations;
                    std::vector<bool> m_alive;
                    std::vector<uint32_t> m_free;
                };

                // -- Device implementation ----------------------------------------

                struct gl_device : public device
                {
                    gl_device();
                    ~gl_device() override;

                    void init() override;
                    void quit() override;

                    buffer create_buffer(const buffer_descriptor& descriptor) override;
                    texture create_texture(const texture_descriptor& descriptor) override;
                    sampler create_sampler(const sampler_descriptor& descriptor) override;
                    shader_module create_shader_module(const shader_module_descriptor& descriptor) override;
                    bind_group_layout create_bind_group_layout(const bind_group_layout_descriptor& descriptor) override;
                    pipeline create_pipeline(const pipeline_descriptor& descriptor) override;
                    bind_group create_bind_group(const bind_group_descriptor& descriptor) override;
                    void update_bind_group(bind_group bind_group_handle,
                                           const std::vector<binding_value>& entries) override;

                    void destroy(buffer handle) override;
                    void destroy(texture handle) override;
                    void destroy(sampler handle) override;
                    void destroy(shader_module handle) override;
                    void destroy(bind_group_layout handle) override;
                    void destroy(pipeline handle) override;
                    void destroy(bind_group handle) override;

                    void write_buffer(buffer buffer_handle, const void* data, size_t size, size_t offset) override;
                    void write_texture(texture texture_handle, const void* data, size_t size) override;
                    void
                    write_cube_face(texture texture_handle, cube_face face, const void* data, size_t size) override;
                    void generate_mipmaps(texture texture_handle) override;

                    render_target swapchain_target() override;
                    void resize_swapchain(uint32_t width, uint32_t height) override;

                    std::unique_ptr<command_encoder> create_command_encoder() override;
                    void submit(std::unique_ptr<command_encoder> encoder) override;

                    // -- Internal accessors used by the encoder -------------------

                    gl_buffer* lookup_buffer(buffer h)
                    {
                        return m_buffers.lookup(h.id);
                    }
                    gl_texture* lookup_texture(texture h)
                    {
                        return m_textures.lookup(h.id);
                    }
                    gl_sampler* lookup_sampler(sampler h)
                    {
                        return m_samplers.lookup(h.id);
                    }
                    gl_pipeline* lookup_pipeline(pipeline h)
                    {
                        return m_pipelines.lookup(h.id);
                    }
                    gl_bind_group* lookup_bind_group(bind_group h)
                    {
                        return m_bind_groups.lookup(h.id);
                    }
                    gl_render_target* lookup_render_target(render_target h)
                    {
                        return m_render_targets.lookup(h.id);
                    }
                    gl_bind_group_layout* lookup_bind_group_layout(bind_group_layout h)
                    {
                        return m_bind_group_layouts.lookup(h.id);
                    }

                private:
                    gl_pool<gl_buffer> m_buffers;
                    gl_pool<gl_texture> m_textures;
                    gl_pool<gl_sampler> m_samplers;
                    gl_pool<gl_shader_module> m_shader_modules;
                    gl_pool<gl_bind_group_layout> m_bind_group_layouts;
                    gl_pool<gl_pipeline> m_pipelines;
                    gl_pool<gl_bind_group> m_bind_groups;
                    gl_pool<gl_render_target> m_render_targets;

                    render_target m_swapchain{};

                    bool m_initialised{false};
                };
            } // namespace opengl
        } // namespace backend
    } // namespace gpu
} // namespace rendering_engine
