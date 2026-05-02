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
 * @brief Backend-agnostic enums for the @ref rendering_engine::gpu device.
 *
 * Values are abstract names — backends translate them to their own native
 * constants (the OpenGL backend lives in @c gpu/backend/opengl/gl_translate.hpp).
 * No header in this directory ever names a backend-specific type.
 */

#pragma once

#include <cstdint>

namespace rendering_engine
{
    namespace gpu
    {
        // Vertex attribute element types. The @c float32 and @c uint32 variants
        // cover everything the engine currently uploads; the smaller integer
        // and packed types are placeholders for future use.
        enum class scalar_type
        {
            float32,
            int32,
            uint32,
            int16,
            uint16,
            int8,
            uint8,
        };

        // Texel formats. Only the formats the engine actually creates today
        // are listed; broadening this is a one-line change in the backend
        // translation table.
        enum class texture_format
        {
            rgba8_unorm,
            rgb8_unorm,
            r8_unorm,
            rgba16_float,
            rgba32_float,
            depth24,
            depth32_float,
            depth24_stencil8,
        };

        // Index buffer element width.
        enum class index_format
        {
            uint16,
            uint32,
        };

        // Draw-call primitive topology.
        enum class primitive_topology
        {
            triangles,
            lines,
            points,
        };

        enum class blend_factor
        {
            zero,
            one,
            src_color,
            one_minus_src_color,
            dst_color,
            one_minus_dst_color,
            src_alpha,
            one_minus_src_alpha,
            dst_alpha,
            one_minus_dst_alpha,
        };

        enum class blend_op
        {
            add,
            subtract,
            reverse_subtract,
            min,
            max,
        };

        enum class compare_function
        {
            never,
            less,
            equal,
            less_equal,
            greater,
            not_equal,
            greater_equal,
            always,
        };

        enum class cull_mode
        {
            none,
            front,
            back,
        };

        // Triangle winding for the front face. @c counter_clockwise matches
        // the engine's existing meshes; @c clockwise is provided for
        // imported assets that bake the opposite convention.
        enum class front_face
        {
            counter_clockwise,
            clockwise,
        };

        // Polygon rasterization mode. @c fill is the default; @c line
        // produces wireframe debug views.
        enum class polygon_mode
        {
            fill,
            line,
            point,
        };

        enum class address_mode
        {
            clamp_edge,
            clamp_border,
            repeat,
            mirrored_repeat,
        };

        enum class filter_mode
        {
            nearest,
            linear,
        };

        enum class mipmap_mode
        {
            none,
            nearest,
            linear,
        };

        enum class shader_stage
        {
            vertex,
            fragment,
            geometry,
        };

        enum class texture_dimension
        {
            d2,
            cube,
        };

        // Cube-map face index. Order matches GL convention; the backend
        // translates to native values.
        enum class cube_face
        {
            positive_x,
            negative_x,
            positive_y,
            negative_y,
            positive_z,
            negative_z,
        };

        // Render-pass attachment load behaviour.
        enum class load_op
        {
            load,
            clear,
            dont_care,
        };

        // Render-pass attachment store behaviour.
        enum class store_op
        {
            store,
            dont_care,
        };

        // Buffer usage hint passed at create time. Matches the GL static /
        // dynamic / stream draw split for a clean translation; on Vulkan
        // / D3D12 backends this would fold into staging/visibility flags.
        enum class buffer_usage_hint
        {
            static_data,
            dynamic_data,
            stream_data,
        };

        // Bitmask of allowed bind points for a buffer. A buffer can be
        // valid for multiple bind points simultaneously (e.g. a vertex
        // buffer that is also a copy destination). Combine with @c |.
        using buffer_usage = uint32_t;
        constexpr buffer_usage buffer_usage_vertex = 1u << 0;
        constexpr buffer_usage buffer_usage_index = 1u << 1;
        constexpr buffer_usage buffer_usage_uniform = 1u << 2;
        constexpr buffer_usage buffer_usage_copy_dst = 1u << 3;
    } // namespace gpu
} // namespace rendering_engine
