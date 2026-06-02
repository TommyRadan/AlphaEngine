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
} // namespace rendering_engine
