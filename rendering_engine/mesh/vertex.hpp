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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <core/math/math.hpp>

namespace rendering_engine
{
    struct vertex_position
    {
        core::math::vec3 pos;
    };

    struct vertex_position_color
    {
        core::math::vec3 pos;
        core::math::vec3 color;
    };

    struct vertex_position_uv
    {
        core::math::vec3 pos;
        core::math::vec2 uv;
    };

    struct vertex_position_normal
    {
        core::math::vec3 pos;
        core::math::vec3 normal;
    };

    struct vertex_position_color_normal
    {
        core::math::vec3 pos;
        core::math::vec3 color;
        core::math::vec3 normal;
    };

    struct vertex_position_uv_normal
    {
        core::math::vec3 pos;
        core::math::vec2 uv;
        core::math::vec3 normal;
    };

    // The @c tangent is stored as a @c vec4 so the bitangent can be
    // reconstructed in the shader as @c cross(normal, tangent.xyz) *
    // tangent.w. The @c .w component carries the handedness sign
    // (+1 or -1) of the UV winding so mirrored geometry flips the
    // bitangent correctly.
    struct vertex_position_uv_normal_tangent
    {
        core::math::vec3 pos;
        core::math::vec2 uv;
        core::math::vec3 normal;
        core::math::vec4 tangent;
    };

    // Names the interleaved record layout of a vertex stream so a mesh and
    // the material drawing it can be checked against each other before a
    // draw is issued. Each named value maps one-to-one onto the vertex
    // struct above of the same name; @c custom marks a record the engine
    // has no struct for (an importer's own layout) and can only be
    // stride-checked.
    enum class vertex_format : uint8_t
    {
        position,
        position_color,
        position_uv,
        position_normal,
        position_color_normal,
        position_uv_normal,
        position_uv_normal_tangent,
        custom,
    };

    // Byte stride of one record of @p format; 0 for @c custom, whose
    // stride is only known to whoever built the record.
    constexpr uint32_t vertex_format_stride(vertex_format format)
    {
        switch (format)
        {
        case vertex_format::position:
            return sizeof(vertex_position);
        case vertex_format::position_color:
            return sizeof(vertex_position_color);
        case vertex_format::position_uv:
            return sizeof(vertex_position_uv);
        case vertex_format::position_normal:
            return sizeof(vertex_position_normal);
        case vertex_format::position_color_normal:
            return sizeof(vertex_position_color_normal);
        case vertex_format::position_uv_normal:
            return sizeof(vertex_position_uv_normal);
        case vertex_format::position_uv_normal_tangent:
            return sizeof(vertex_position_uv_normal_tangent);
        case vertex_format::custom:
            return 0;
        }
        return 0;
    }

    // Stable, human-readable name of @p format for log lines and cache keys.
    constexpr const char* vertex_format_name(vertex_format format)
    {
        switch (format)
        {
        case vertex_format::position:
            return "position";
        case vertex_format::position_color:
            return "position_color";
        case vertex_format::position_uv:
            return "position_uv";
        case vertex_format::position_normal:
            return "position_normal";
        case vertex_format::position_color_normal:
            return "position_color_normal";
        case vertex_format::position_uv_normal:
            return "position_uv_normal";
        case vertex_format::position_uv_normal_tangent:
            return "position_uv_normal_tangent";
        case vertex_format::custom:
            return "custom";
        }
        return "unknown";
    }

    namespace detail
    {
        // One attribute channel of a named record, in interleave order.
        enum class vertex_channel : uint8_t
        {
            none,
            position,
            color,
            uv,
            normal,
            tangent,
        };

        // The channel sequence of each named format. Every named record is a
        // leading run of these channels with no padding, so two formats agree
        // on the offset of every attribute they both carry exactly when one
        // sequence is a prefix of the other. @c custom has no sequence.
        constexpr std::array<vertex_channel, 4> vertex_format_channels(vertex_format format)
        {
            using c = vertex_channel;
            switch (format)
            {
            case vertex_format::position:
                return {c::position, c::none, c::none, c::none};
            case vertex_format::position_color:
                return {c::position, c::color, c::none, c::none};
            case vertex_format::position_uv:
                return {c::position, c::uv, c::none, c::none};
            case vertex_format::position_normal:
                return {c::position, c::normal, c::none, c::none};
            case vertex_format::position_color_normal:
                return {c::position, c::color, c::normal, c::none};
            case vertex_format::position_uv_normal:
                return {c::position, c::uv, c::normal, c::none};
            case vertex_format::position_uv_normal_tangent:
                return {c::position, c::uv, c::normal, c::tangent};
            case vertex_format::custom:
                return {c::none, c::none, c::none, c::none};
            }
            return {c::none, c::none, c::none, c::none};
        }
    } // namespace detail

    // Whether a stream laid out as @p mesh can be fetched by a pipeline that
    // reads the channels of @p required. True when @p required's channels
    // form a leading prefix of @p mesh's channels: every attribute the
    // pipeline reads then sits at the offset it expects and the record is at
    // least as wide as the last one it reaches. So a position+uv pipeline
    // reads a position+uv+normal(+tangent) stream correctly, while a
    // tangent-reading pipeline rejects a 32-byte position+uv+normal record
    // instead of fetching 16 bytes past the end of every vertex. @c custom on
    // either side carries no channel information and is reported as
    // incompatible; callers that accept custom records fall back to a plain
    // stride check.
    constexpr bool vertex_format_compatible(vertex_format mesh, vertex_format required)
    {
        if (mesh == vertex_format::custom || required == vertex_format::custom)
        {
            return false;
        }
        const auto have = detail::vertex_format_channels(mesh);
        const auto need = detail::vertex_format_channels(required);
        for (std::size_t i = 0; i < need.size(); ++i)
        {
            if (need[i] == detail::vertex_channel::none)
            {
                return true;
            }
            if (need[i] != have[i])
            {
                return false;
            }
        }
        return true;
    }

    // Maps a vertex struct to its @ref vertex_format so typed builders
    // (@c mesh_data::from_vertices) record the format without spelling it
    // out. Unlisted types are @c custom.
    template<typename VertexT>
    struct vertex_format_of
    {
        static constexpr vertex_format value = vertex_format::custom;
    };

    template<>
    struct vertex_format_of<vertex_position>
    {
        static constexpr vertex_format value = vertex_format::position;
    };

    template<>
    struct vertex_format_of<vertex_position_color>
    {
        static constexpr vertex_format value = vertex_format::position_color;
    };

    template<>
    struct vertex_format_of<vertex_position_uv>
    {
        static constexpr vertex_format value = vertex_format::position_uv;
    };

    template<>
    struct vertex_format_of<vertex_position_normal>
    {
        static constexpr vertex_format value = vertex_format::position_normal;
    };

    template<>
    struct vertex_format_of<vertex_position_color_normal>
    {
        static constexpr vertex_format value = vertex_format::position_color_normal;
    };

    template<>
    struct vertex_format_of<vertex_position_uv_normal>
    {
        static constexpr vertex_format value = vertex_format::position_uv_normal;
    };

    template<>
    struct vertex_format_of<vertex_position_uv_normal_tangent>
    {
        static constexpr vertex_format value = vertex_format::position_uv_normal_tangent;
    };

    template<typename VertexT>
    inline constexpr vertex_format vertex_format_of_v = vertex_format_of<VertexT>::value;

    // The named strides are what the built-in materials' hand-written
    // attribute offsets (0 / 12 / 20 / 32) assume; pin them here so a
    // change to a vertex struct fails to compile rather than silently
    // shifting every material's layout.
    static_assert(vertex_format_stride(vertex_format::position) == 12);
    static_assert(vertex_format_stride(vertex_format::position_color) == 24);
    static_assert(vertex_format_stride(vertex_format::position_uv) == 20);
    static_assert(vertex_format_stride(vertex_format::position_normal) == 24);
    static_assert(vertex_format_stride(vertex_format::position_color_normal) == 36);
    static_assert(vertex_format_stride(vertex_format::position_uv_normal) == 32);
    static_assert(vertex_format_stride(vertex_format::position_uv_normal_tangent) == 48);
} // namespace rendering_engine
