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

#include <cstdint>
#include <vector>

namespace rendering_engine
{
    struct light;

    // Fixed UBO capacities. Excess lights past these counts are dropped
    // when packing; bump the constants (and the matching GLSL array
    // sizes in the consuming material) together if more are needed.
    inline constexpr uint32_t max_directional_lights = 4;
    inline constexpr uint32_t max_point_lights = 16;

    // std140 mirror of one directional light entry (32 bytes). Every
    // member is vec4-aligned so the C++ layout matches the shared
    // @c DirectionalLight GLSL struct byte-for-byte.
    struct gpu_directional_light
    {
        float direction[4]; // xyz normalized direction, w unused
        float color[4];     // rgb radiance pre-multiplied by intensity, a unused
    };

    // std140 mirror of one point light entry (48 bytes).
    struct gpu_point_light
    {
        float position[4];    // xyz world position, w unused
        float color[4];       // rgb radiance pre-multiplied by intensity, a unused
        float attenuation[4]; // x range, y constant, z linear, w quadratic
    };

    // std140 mirror of the per-frame @c Lights block bound at slot 0,
    // binding 2. The layout it must match in GLSL is:
    //
    //   layout(set = 0, binding = 2, std140) uniform Lights
    //   {
    //       vec4 ambient;                                // rgb sum, a unused
    //       ivec4 counts;                                // x directional, y point
    //       DirectionalLight directional[MAX_DIRECTIONAL];
    //       PointLight point[MAX_POINT];
    //   } u_lights;
    //
    // The two scalar counts plus padding fill the ivec4 so the light
    // arrays start on a 16-byte boundary, as std140 requires.
    struct gpu_lights
    {
        float ambient[4];

        int32_t directional_count;
        int32_t point_count;
        int32_t pad0;
        int32_t pad1;

        gpu_directional_light directional[max_directional_lights];
        gpu_point_light point[max_point_lights];
    };

    // Accumulate @p lights into @p out: ambient lights sum into a single
    // term, directional / point lights fill their arrays up to capacity,
    // and the counts are written. @p out is fully overwritten.
    void pack_lights(const std::vector<light*>& lights, gpu_lights& out);
} // namespace rendering_engine
