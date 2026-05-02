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

#include <glad/gl.h>

#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        namespace backend
        {
            namespace opengl
            {
                GLenum to_gl_primitive(primitive_topology topology);
                GLenum to_gl_blend_factor(blend_factor factor);
                GLenum to_gl_blend_op(blend_op op);
                GLenum to_gl_compare(compare_function fn);
                GLenum to_gl_cull_face(cull_mode mode);
                GLenum to_gl_front_face(front_face face);
                GLenum to_gl_polygon_mode(polygon_mode mode);
                GLenum to_gl_address_mode(address_mode mode);
                GLenum to_gl_min_filter(filter_mode min, mipmap_mode mip);
                GLenum to_gl_mag_filter(filter_mode mag);
                GLenum to_gl_shader_stage(shader_stage stage);
                GLenum to_gl_index_type(index_format format);
                GLenum to_gl_scalar(scalar_type type);
                GLenum to_gl_buffer_usage_hint(buffer_usage_hint hint);
                GLenum to_gl_cube_face(cube_face face);

                // For a texture create: returns the @c (internal_format,
                // upload_format, upload_type) triple suitable for
                // @c glTexImage2D. The internal format is what the GPU
                // stores; the upload format/type describe @c data.
                struct gl_texture_format
                {
                    GLint internal_format;
                    GLenum upload_format;
                    GLenum upload_type;
                };

                gl_texture_format to_gl_texture_format(texture_format format);
            } // namespace opengl
        } // namespace backend
    } // namespace gpu
} // namespace rendering_engine
