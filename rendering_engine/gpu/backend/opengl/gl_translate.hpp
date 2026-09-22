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
 * @file gl_translate.hpp
 * @brief Mapping table between backend-agnostic @c gpu::* enums and the
 *        OpenGL constants they correspond to. The only place in the
 *        backend that knows about both vocabularies.
 */

#pragma once

#include <cstdint>

#include <glad/gl.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    GLenum to_gl_primitive(primitive_topology topology);
    GLenum to_gl_blend_factor(blend_factor factor);
    GLenum to_gl_blend_op(blend_op op);
    GLenum to_gl_compare(compare_function fn);
    GLenum to_gl_cull_face(cull_mode mode);
    GLenum to_gl_front_face(front_face face);
    GLenum to_gl_polygon_mode(polygon_mode mode);
    GLenum to_gl_address_mode(address_mode mode);
    // @p mip is the chain filter actually applied — pass it through
    // @ref effective_mipmap_filter first so a mipmapped texture samples
    // its chain and a single-level one never asks for a mip filter.
    GLenum to_gl_min_filter(filter_mode min, mipmap_mode mip);
    GLenum to_gl_mag_filter(filter_mode mag);
    GLenum to_gl_shader_stage(shader_stage stage);
    GLenum to_gl_index_type(index_format format);
    GLenum to_gl_scalar(scalar_type type);
    // Byte width of one @p type component, for deriving a record stride
    // from a layout's attributes.
    uint32_t to_gl_scalar_bytes(scalar_type type);
    // True for the integer scalar types, which bake through
    // @c glVertexArrayAttribIFormat unless the attribute is normalised.
    bool is_gl_integer_scalar(scalar_type type);
    GLenum to_gl_buffer_usage_hint(buffer_usage_hint hint);
    GLenum to_gl_cube_face(cube_face face);
    GLenum to_gl_texture_target(texture_dimension dimension);
    GLenum to_gl_storage_access(storage_access access);

    // Translate the engine's @c access_flag bitmask into the
    // matching @c GL_*_BARRIER_BIT bitmask consumed by
    // @c glMemoryBarrier. Only the @c dst_access mask drives the
    // result; on OpenGL the source mask is implicit (the API
    // orders all prior writes against the requested category).
    GLbitfield to_gl_memory_barrier_bits(access_flag dst_access);

    // For a texture create: returns the @c (internal_format,
    // upload_format, upload_type) triple for @c glTextureStorage2D /
    // @c glTextureSubImage2D. The internal format is what the GPU
    // stores; the upload format/type describe @c data.
    struct gl_texture_format
    {
        GLenum internal_format;
        GLenum upload_format;
        GLenum upload_type;
    };

    gl_texture_format to_gl_texture_format(texture_format format);

    // Bytes per texel of the tightly packed client layout
    // @ref to_gl_texture_format's upload format / type describe, for
    // validating the byte count handed to a texture write.
    uint32_t to_gl_texel_bytes(texture_format format);

    // True for the packed depth-stencil format, which attaches to
    // @c GL_DEPTH_STENCIL_ATTACHMENT rather than @c GL_DEPTH_ATTACHMENT.
    bool is_gl_depth_stencil_format(texture_format format);

    // Names for log output: @c glGetError codes and the KHR_debug
    // source / type enums. Never null.
    const char* gl_error_name(GLenum error);
    const char* gl_debug_source_name(GLenum source);
    const char* gl_debug_type_name(GLenum type);
} // namespace rendering_engine::gpu::backend::opengl
