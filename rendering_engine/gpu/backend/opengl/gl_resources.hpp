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
 * @file gl_resources.hpp
 * @brief Per-resource GL-side record types.
 *
 * Each @c gl_pool<T> in @ref gl_device stores values of one of these
 * structs. They live in their own header so individual resource impl
 * files (gl_buffer.cpp, gl_texture.cpp, ...) can consume them without
 * pulling in the full @ref gl_device declaration.
 */

#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::opengl
{
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
} // namespace rendering_engine::gpu::backend::opengl
