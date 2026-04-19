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
 * @file types.hpp
 * @brief Backend-agnostic enum types used across the RHI interface.
 */

#pragma once

#include <cstdint>

namespace rhi
{
    /**
     * @brief Primitive topology for @ref rhi::draw_arrays / @ref rhi::draw_elements.
     */
    enum class primitive_type
    {
        triangles,
        lines,
        points
    };

    /**
     * @brief Optional device state toggled via @ref rhi::enable / @ref rhi::disable.
     */
    enum class capability
    {
        depth_test,
        stencil_test,
        cull_face,
        rasterizer_discard,
        blend
    };

    /**
     * @brief Framebuffer attachments that can be cleared by @ref rhi::clear.
     */
    enum class clear_buffer : uint32_t
    {
        color = 1u << 0,
        depth = 1u << 1,
        stencil = 1u << 2
    };

    inline clear_buffer operator|(clear_buffer lhs, clear_buffer rhs)
    {
        return static_cast<clear_buffer>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    /** @brief Element type for @ref rhi::vertex_array attributes and index buffers. */
    enum class element_type
    {
        byte_type,
        unsigned_byte_type,
        short_type,
        unsigned_short_type,
        int_type,
        unsigned_int_type,
        float_type,
        double_type
    };

    /** @brief Stage of a shader object handed to @ref rhi::create_shader. */
    enum class shader_stage
    {
        vertex,
        fragment,
        geometry
    };

    /** @brief Buffer-usage hint passed to @ref rhi::vertex_buffer uploads. */
    enum class buffer_usage
    {
        stream_draw,
        static_draw,
        dynamic_draw,
        stream_read,
        static_read,
        dynamic_read,
        stream_copy,
        static_copy,
        dynamic_copy
    };

    /** @brief Texture wrapping mode for the R/S/T axes. */
    enum class wrap_mode
    {
        clamp_edge,
        clamp_border,
        repeat,
        mirrored_repeat
    };

    /** @brief Texture min/mag filter. */
    enum class filter_mode
    {
        nearest,
        linear,
        nearest_mipmap_nearest,
        linear_mipmap_nearest,
        nearest_mipmap_linear,
        linear_mipmap_linear
    };

    /** @brief Pixel format of source data supplied to @c texture::image2d. */
    enum class pixel_format
    {
        red,
        rgb,
        bgr,
        rgba,
        bgra
    };

    /** @brief Storage format of a texture on the GPU. */
    enum class internal_format
    {
        red,
        rgb,
        rgba,
        srgb8,
        srgb8_alpha8,
        depth_component,
        depth_component16,
        depth_component24,
        depth_component32f
    };
} // namespace rhi
