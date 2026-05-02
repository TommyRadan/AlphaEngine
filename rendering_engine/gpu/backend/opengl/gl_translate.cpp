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

#include <rendering_engine/gpu/backend/opengl/gl_translate.hpp>

namespace rendering_engine::gpu::backend::opengl
{
    GLenum to_gl_primitive(primitive_topology topology)
    {
        switch (topology)
        {
        case primitive_topology::triangles:
            return GL_TRIANGLES;
        case primitive_topology::lines:
            return GL_LINES;
        case primitive_topology::points:
            return GL_POINTS;
        }
        return GL_TRIANGLES;
    }

    GLenum to_gl_blend_factor(blend_factor factor)
    {
        switch (factor)
        {
        case blend_factor::zero:
            return GL_ZERO;
        case blend_factor::one:
            return GL_ONE;
        case blend_factor::src_color:
            return GL_SRC_COLOR;
        case blend_factor::one_minus_src_color:
            return GL_ONE_MINUS_SRC_COLOR;
        case blend_factor::dst_color:
            return GL_DST_COLOR;
        case blend_factor::one_minus_dst_color:
            return GL_ONE_MINUS_DST_COLOR;
        case blend_factor::src_alpha:
            return GL_SRC_ALPHA;
        case blend_factor::one_minus_src_alpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case blend_factor::dst_alpha:
            return GL_DST_ALPHA;
        case blend_factor::one_minus_dst_alpha:
            return GL_ONE_MINUS_DST_ALPHA;
        }
        return GL_ONE;
    }

    GLenum to_gl_blend_op(blend_op op)
    {
        switch (op)
        {
        case blend_op::add:
            return GL_FUNC_ADD;
        case blend_op::subtract:
            return GL_FUNC_SUBTRACT;
        case blend_op::reverse_subtract:
            return GL_FUNC_REVERSE_SUBTRACT;
        case blend_op::min:
            return GL_MIN;
        case blend_op::max:
            return GL_MAX;
        }
        return GL_FUNC_ADD;
    }

    GLenum to_gl_compare(compare_function fn)
    {
        switch (fn)
        {
        case compare_function::never:
            return GL_NEVER;
        case compare_function::less:
            return GL_LESS;
        case compare_function::equal:
            return GL_EQUAL;
        case compare_function::less_equal:
            return GL_LEQUAL;
        case compare_function::greater:
            return GL_GREATER;
        case compare_function::not_equal:
            return GL_NOTEQUAL;
        case compare_function::greater_equal:
            return GL_GEQUAL;
        case compare_function::always:
            return GL_ALWAYS;
        }
        return GL_LESS;
    }

    GLenum to_gl_cull_face(cull_mode mode)
    {
        switch (mode)
        {
        case cull_mode::none:
            return GL_NONE;
        case cull_mode::front:
            return GL_FRONT;
        case cull_mode::back:
            return GL_BACK;
        }
        return GL_BACK;
    }

    GLenum to_gl_front_face(front_face face)
    {
        switch (face)
        {
        case front_face::counter_clockwise:
            return GL_CCW;
        case front_face::clockwise:
            return GL_CW;
        }
        return GL_CCW;
    }

    GLenum to_gl_polygon_mode(polygon_mode mode)
    {
        switch (mode)
        {
        case polygon_mode::fill:
            return GL_FILL;
        case polygon_mode::line:
            return GL_LINE;
        case polygon_mode::point:
            return GL_POINT;
        }
        return GL_FILL;
    }

    GLenum to_gl_address_mode(address_mode mode)
    {
        switch (mode)
        {
        case address_mode::clamp_edge:
            return GL_CLAMP_TO_EDGE;
        case address_mode::clamp_border:
            return GL_CLAMP_TO_BORDER;
        case address_mode::repeat:
            return GL_REPEAT;
        case address_mode::mirrored_repeat:
            return GL_MIRRORED_REPEAT;
        }
        return GL_REPEAT;
    }

    GLenum to_gl_min_filter(filter_mode min, mipmap_mode mip)
    {
        if (mip == mipmap_mode::none)
        {
            return min == filter_mode::linear ? GL_LINEAR : GL_NEAREST;
        }
        if (min == filter_mode::linear)
        {
            return mip == mipmap_mode::linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
        }
        return mip == mipmap_mode::linear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    }

    GLenum to_gl_mag_filter(filter_mode mag)
    {
        return mag == filter_mode::linear ? GL_LINEAR : GL_NEAREST;
    }

    GLenum to_gl_shader_stage(shader_stage stage)
    {
        switch (stage)
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

    GLenum to_gl_index_type(index_format format)
    {
        return format == index_format::uint16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    }

    GLenum to_gl_scalar(scalar_type type)
    {
        switch (type)
        {
        case scalar_type::float32:
            return GL_FLOAT;
        case scalar_type::int32:
            return GL_INT;
        case scalar_type::uint32:
            return GL_UNSIGNED_INT;
        case scalar_type::int16:
            return GL_SHORT;
        case scalar_type::uint16:
            return GL_UNSIGNED_SHORT;
        case scalar_type::int8:
            return GL_BYTE;
        case scalar_type::uint8:
            return GL_UNSIGNED_BYTE;
        }
        return GL_FLOAT;
    }

    GLenum to_gl_buffer_usage_hint(buffer_usage_hint hint)
    {
        switch (hint)
        {
        case buffer_usage_hint::static_data:
            return GL_STATIC_DRAW;
        case buffer_usage_hint::dynamic_data:
            return GL_DYNAMIC_DRAW;
        case buffer_usage_hint::stream_data:
            return GL_STREAM_DRAW;
        }
        return GL_STATIC_DRAW;
    }

    GLenum to_gl_cube_face(cube_face face)
    {
        switch (face)
        {
        case cube_face::positive_x:
            return GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        case cube_face::negative_x:
            return GL_TEXTURE_CUBE_MAP_NEGATIVE_X;
        case cube_face::positive_y:
            return GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
        case cube_face::negative_y:
            return GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
        case cube_face::positive_z:
            return GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
        case cube_face::negative_z:
            return GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
        }
        return GL_TEXTURE_CUBE_MAP_POSITIVE_X;
    }

    gl_texture_format to_gl_texture_format(texture_format format)
    {
        switch (format)
        {
        case texture_format::rgba8_unorm:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case texture_format::rgb8_unorm:
            return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
        case texture_format::r8_unorm:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case texture_format::rgba16_float:
            return {GL_RGBA16F, GL_RGBA, GL_FLOAT};
        case texture_format::rgba32_float:
            return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
        case texture_format::depth24:
            return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE};
        case texture_format::depth32_float:
            return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
        case texture_format::depth24_stencil8:
            return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
        }
        return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
} // namespace rendering_engine::gpu::backend::opengl
