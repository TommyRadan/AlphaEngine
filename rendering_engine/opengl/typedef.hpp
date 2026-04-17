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

#pragma once

#include <cstdint>
#include <glad/gl.h>

namespace rendering_engine
{
    namespace opengl
    {
        enum class type
        {
            byte = GL_BYTE,
            unsigned_byte = GL_UNSIGNED_BYTE,
            // NOLINTBEGIN(readability-identifier-naming) — capitalized to avoid C++ keyword collisions
            Short = GL_SHORT,
            unsigned_short = GL_UNSIGNED_SHORT,
            Int = GL_INT,
            unsigned_int = GL_UNSIGNED_INT,
            Float = GL_FLOAT,
            Double = GL_DOUBLE
            // NOLINTEND(readability-identifier-naming)
        };

        using attribute = GLint;
        using uniform = GLint;

        enum class buffer
        {
            color = GL_COLOR_BUFFER_BIT,
            depth = GL_DEPTH_BUFFER_BIT,
            stencil = GL_STENCIL_BUFFER_BIT
        };

        inline buffer operator|(buffer lft, buffer rht)
        {
            return (buffer)((uint32_t)lft | (uint32_t)rht);
        }

        enum class primitive
        {
            triangles = GL_TRIANGLES,
            lines = GL_LINES,
            points = GL_POINTS,
        };

        enum class capability
        {
            depth_test = GL_DEPTH_TEST,
            stencil_test = GL_STENCIL_TEST,
            cull_face = GL_CULL_FACE,
            rasterizer_discard = GL_RASTERIZER_DISCARD,
            blend = GL_BLEND
        };

        enum class shader_type
        {
            vertex = GL_VERTEX_SHADER,
            fragment = GL_FRAGMENT_SHADER,
            geometry = GL_GEOMETRY_SHADER
        };

        enum class internal_format
        {
            compressed_red = GL_COMPRESSED_RED,
            compressed_red_rgt_c1 = GL_COMPRESSED_RED_RGTC1,
            compressed_rg = GL_COMPRESSED_RG,
            compressed_rgb = GL_COMPRESSED_RGB,
            compressed_rgba = GL_COMPRESSED_RGBA,
            compressed_rgrgt_c2 = GL_COMPRESSED_RG_RGTC2,
            compressed_signed_red_rgt_c1 = GL_COMPRESSED_SIGNED_RED_RGTC1,
            compressed_signed_rgrgt_c2 = GL_COMPRESSED_SIGNED_RG_RGTC2,
            compressed_srgb = GL_COMPRESSED_SRGB,
            depth_stencil = GL_DEPTH_STENCIL,
            depth24_stencil8 = GL_DEPTH24_STENCIL8,
            depth32_f_stencil8 = GL_DEPTH32F_STENCIL8,
            depth_component = GL_DEPTH_COMPONENT,
            depth_component16 = GL_DEPTH_COMPONENT16,
            depth_component24 = GL_DEPTH_COMPONENT24,
            depth_component32_f = GL_DEPTH_COMPONENT32F,
            r16_f = GL_R16F,
            r16_i = GL_R16I,
            r16_s_norm = GL_R16_SNORM,
            r16_ui = GL_R16UI,
            r32_f = GL_R32F,
            r32_i = GL_R32I,
            r32_ui = GL_R32UI,
            r3_g3_b2 = GL_R3_G3_B2,
            r8 = GL_R8,
            r8_i = GL_R8I,
            r8_s_norm = GL_R8_SNORM,
            r8_ui = GL_R8UI,
            red = GL_RED,
            rg = GL_RG,
            r_g16 = GL_RG16,
            r_g16_f = GL_RG16F,
            r_g16_s_norm = GL_RG16_SNORM,
            r_g32_f = GL_RG32F,
            r_g32_i = GL_RG32I,
            r_g32_ui = GL_RG32UI,
            r_g8 = GL_RG8,
            r_g8_i = GL_RG8I,
            r_g8_s_norm = GL_RG8_SNORM,
            r_g8_ui = GL_RG8UI,
            rgb = GL_RGB,
            rg_b10 = GL_RGB10,
            rg_b10_a2 = GL_RGB10_A2,
            rg_b12 = GL_RGB12,
            rg_b16 = GL_RGB16,
            rg_b16_f = GL_RGB16F,
            rg_b16_i = GL_RGB16I,
            rg_b16_ui = GL_RGB16UI,
            rg_b32_f = GL_RGB32F,
            rg_b32_i = GL_RGB32I,
            rg_b32_ui = GL_RGB32UI,
            rg_b4 = GL_RGB4,
            rg_b5 = GL_RGB5,
            rg_b5_a1 = GL_RGB5_A1,
            rg_b8 = GL_RGB8,
            rg_b8_i = GL_RGB8I,
            rg_b8_ui = GL_RGB8UI,
            rg_b9_e5 = GL_RGB9_E5,
            rgba = GL_RGBA,
            rgb_a12 = GL_RGBA12,
            rgb_a16 = GL_RGBA16,
            rgb_a16_f = GL_RGBA16F,
            rgb_a16_i = GL_RGBA16I,
            rgb_a16_ui = GL_RGBA16UI,
            rgb_a2 = GL_RGBA2,
            rgb_a32_f = GL_RGBA32F,
            rgb_a32_i = GL_RGBA32I,
            rgb_a32_ui = GL_RGBA32UI,
            rgb_a4 = GL_RGBA4,
            rgb_a8 = GL_RGBA8,
            rgb_a8_ui = GL_RGBA8UI,
            srg_b8 = GL_SRGB8,
            srg_b8_a8 = GL_SRGB8_ALPHA8,
            srgba = GL_SRGB_ALPHA
        };

        enum class format
        {
            red = GL_RED,
            rgb = GL_RGB,
            bgr = GL_BGR,
            rgba = GL_RGBA,
            bgra = GL_BGRA
        };

        enum class data_type
        {
            byte = GL_BYTE,
            unsigned_byte = GL_UNSIGNED_BYTE,
            // NOLINTBEGIN(readability-identifier-naming) — capitalized to avoid C++ keyword collisions
            Short = GL_SHORT,
            unsigned_short = GL_UNSIGNED_SHORT,
            Int = GL_INT,
            unsigned_int = GL_UNSIGNED_INT,
            Float = GL_FLOAT,
            Double = GL_DOUBLE,
            // NOLINTEND(readability-identifier-naming)

            unsigned_byte332 = GL_UNSIGNED_BYTE_3_3_2,
            unsigned_byte233_rev = GL_UNSIGNED_BYTE_2_3_3_REV,
            unsigned_short565 = GL_UNSIGNED_SHORT_5_6_5,
            unsigned_short565_rev = GL_UNSIGNED_SHORT_5_6_5,
            unsigned_short4444 = GL_UNSIGNED_SHORT_4_4_4_4,
            unsigned_short4444_rev = GL_UNSIGNED_SHORT_4_4_4_4_REV,
            unsigned_short5551 = GL_UNSIGNED_SHORT_5_5_5_1,
            unsigned_short1555_rev = GL_UNSIGNED_SHORT_1_5_5_5_REV,
            unsigned_int8888 = GL_UNSIGNED_INT_8_8_8_8,
            unsigned_int8888_rev = GL_UNSIGNED_INT_8_8_8_8_REV,
            unsigned_int101010102 = GL_UNSIGNED_INT_10_10_10_2
        };

        enum class wrapping
        {
            clamp_edge = GL_CLAMP_TO_EDGE,
            clamp_border = GL_CLAMP_TO_BORDER,
            repeat = GL_REPEAT,
            mirrored_repeat = GL_MIRRORED_REPEAT
        };

        enum class filter
        {
            nearest = GL_NEAREST,
            linear = GL_LINEAR,
            nearest_mipmap_nearest = GL_NEAREST_MIPMAP_NEAREST,
            linear_mipmap_nearest = GL_LINEAR_MIPMAP_NEAREST,
            nearest_mipmap_linear = GL_NEAREST_MIPMAP_LINEAR,
            linear_mipmap_linear = GL_LINEAR_MIPMAP_LINEAR
        };

        enum class buffer_usage
        {
            stream_draw = GL_STREAM_DRAW,
            stream_read = GL_STREAM_READ,
            stream_copy = GL_STREAM_COPY,
            static_draw = GL_STATIC_DRAW,
            static_read = GL_STATIC_READ,
            static_copy = GL_STATIC_COPY,
            dynamic_draw = GL_DYNAMIC_DRAW,
            dynamic_read = GL_DYNAMIC_READ,
            dynamic_copy = GL_DYNAMIC_COPY
        };
    } // namespace opengl
} // namespace rendering_engine
