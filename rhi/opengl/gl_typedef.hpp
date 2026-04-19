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
 * @file gl_typedef.hpp
 * @brief Backend-private translation of RHI enums to GLenum values.
 *
 * This header is internal to the OpenGL backend and must not be included
 * from files outside @c rhi/opengl/ — it is the one place where @c glad/gl.h
 * meets the RHI type system.
 */

#pragma once

#include <cstdint>
#include <glad/gl.h>

#include <rhi/types.hpp>

namespace rhi
{
    namespace opengl
    {
        inline GLenum to_gl(primitive_type p)
        {
            switch (p)
            {
            case primitive_type::triangles:
                return GL_TRIANGLES;
            case primitive_type::lines:
                return GL_LINES;
            case primitive_type::points:
                return GL_POINTS;
            }
            return GL_TRIANGLES;
        }

        inline GLenum to_gl(capability c)
        {
            switch (c)
            {
            case capability::depth_test:
                return GL_DEPTH_TEST;
            case capability::stencil_test:
                return GL_STENCIL_TEST;
            case capability::cull_face:
                return GL_CULL_FACE;
            case capability::rasterizer_discard:
                return GL_RASTERIZER_DISCARD;
            case capability::blend:
                return GL_BLEND;
            }
            return GL_DEPTH_TEST;
        }

        inline GLbitfield to_gl_clear_mask(clear_buffer buffers)
        {
            GLbitfield mask = 0;
            auto bits = static_cast<uint32_t>(buffers);
            if ((bits & static_cast<uint32_t>(clear_buffer::color)) != 0U)
            {
                mask |= GL_COLOR_BUFFER_BIT;
            }
            if ((bits & static_cast<uint32_t>(clear_buffer::depth)) != 0U)
            {
                mask |= GL_DEPTH_BUFFER_BIT;
            }
            if ((bits & static_cast<uint32_t>(clear_buffer::stencil)) != 0U)
            {
                mask |= GL_STENCIL_BUFFER_BIT;
            }
            return mask;
        }

        inline GLenum to_gl(element_type t)
        {
            switch (t)
            {
            case element_type::byte_type:
                return GL_BYTE;
            case element_type::unsigned_byte_type:
                return GL_UNSIGNED_BYTE;
            case element_type::short_type:
                return GL_SHORT;
            case element_type::unsigned_short_type:
                return GL_UNSIGNED_SHORT;
            case element_type::int_type:
                return GL_INT;
            case element_type::unsigned_int_type:
                return GL_UNSIGNED_INT;
            case element_type::float_type:
                return GL_FLOAT;
            case element_type::double_type:
                return GL_DOUBLE;
            }
            return GL_FLOAT;
        }

        inline GLenum to_gl(shader_stage s)
        {
            switch (s)
            {
            case shader_stage::vertex:
                return GL_VERTEX_SHADER;
            case shader_stage::fragment:
                return GL_FRAGMENT_SHADER;
            case shader_stage::geometry:
                return GL_GEOMETRY_SHADER;
            }
            return GL_VERTEX_SHADER;
        }

        inline GLenum to_gl(buffer_usage u)
        {
            switch (u)
            {
            case buffer_usage::stream_draw:
                return GL_STREAM_DRAW;
            case buffer_usage::static_draw:
                return GL_STATIC_DRAW;
            case buffer_usage::dynamic_draw:
                return GL_DYNAMIC_DRAW;
            case buffer_usage::stream_read:
                return GL_STREAM_READ;
            case buffer_usage::static_read:
                return GL_STATIC_READ;
            case buffer_usage::dynamic_read:
                return GL_DYNAMIC_READ;
            case buffer_usage::stream_copy:
                return GL_STREAM_COPY;
            case buffer_usage::static_copy:
                return GL_STATIC_COPY;
            case buffer_usage::dynamic_copy:
                return GL_DYNAMIC_COPY;
            }
            return GL_STATIC_DRAW;
        }

        inline GLint to_gl(wrap_mode w)
        {
            switch (w)
            {
            case wrap_mode::clamp_edge:
                return GL_CLAMP_TO_EDGE;
            case wrap_mode::clamp_border:
                return GL_CLAMP_TO_BORDER;
            case wrap_mode::repeat:
                return GL_REPEAT;
            case wrap_mode::mirrored_repeat:
                return GL_MIRRORED_REPEAT;
            }
            return GL_REPEAT;
        }

        inline GLint to_gl(filter_mode f)
        {
            switch (f)
            {
            case filter_mode::nearest:
                return GL_NEAREST;
            case filter_mode::linear:
                return GL_LINEAR;
            case filter_mode::nearest_mipmap_nearest:
                return GL_NEAREST_MIPMAP_NEAREST;
            case filter_mode::linear_mipmap_nearest:
                return GL_LINEAR_MIPMAP_NEAREST;
            case filter_mode::nearest_mipmap_linear:
                return GL_NEAREST_MIPMAP_LINEAR;
            case filter_mode::linear_mipmap_linear:
                return GL_LINEAR_MIPMAP_LINEAR;
            }
            return GL_LINEAR;
        }

        inline GLenum to_gl(pixel_format p)
        {
            switch (p)
            {
            case pixel_format::red:
                return GL_RED;
            case pixel_format::rgb:
                return GL_RGB;
            case pixel_format::bgr:
                return GL_BGR;
            case pixel_format::rgba:
                return GL_RGBA;
            case pixel_format::bgra:
                return GL_BGRA;
            }
            return GL_RGBA;
        }

        inline GLint to_gl(internal_format i)
        {
            switch (i)
            {
            case internal_format::red:
                return GL_RED;
            case internal_format::rgb:
                return GL_RGB;
            case internal_format::rgba:
                return GL_RGBA;
            case internal_format::srgb8:
                return GL_SRGB8;
            case internal_format::srgb8_alpha8:
                return GL_SRGB8_ALPHA8;
            case internal_format::depth_component:
                return GL_DEPTH_COMPONENT;
            case internal_format::depth_component16:
                return GL_DEPTH_COMPONENT16;
            case internal_format::depth_component24:
                return GL_DEPTH_COMPONENT24;
            case internal_format::depth_component32f:
                return GL_DEPTH_COMPONENT32F;
            }
            return GL_RGBA;
        }
    } // namespace opengl
} // namespace rhi
