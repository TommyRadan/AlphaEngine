// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <span>

#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine
{
    // Fixed UBO capacities. Excess lights past these counts are dropped
    // when packing; bump the constants (and the matching GLSL array
    // sizes in the consuming material) together if more are needed.
    inline constexpr uint32_t max_directional_lights = 4;
    inline constexpr uint32_t max_point_lights = 16;
    inline constexpr uint32_t max_spot_lights = 4;

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

    // std140 mirror of one spot light entry (80 bytes). Range attenuation
    // matches gpu_point_light; the cone term is the extra @c cone field
    // (x cos(outer angle), y cos(inner angle)), evaluated in the shared
    // lighting include so every consumer applies it the same way.
    struct gpu_spot_light
    {
        float position[4];    // xyz world position, w unused
        float direction[4];   // xyz normalized direction, w unused
        float color[4];       // rgb radiance pre-multiplied by intensity, a unused
        float attenuation[4]; // x range, y constant, z linear, w quadratic
        float cone[4];        // x cos(outer angle), y cos(inner angle), zw unused
    };

    // std140 mirror of the per-frame @c Lights block bound at slot 0,
    // binding 2. The layout it must match in GLSL is:
    //
    //   layout(set = 0, binding = 2, std140) uniform Lights
    //   {
    //       vec4 ambient;                                // rgb sum, a unused
    //       ivec4 counts;                                // x directional, y point, z spot
    //       DirectionalLight directional[MAX_DIRECTIONAL];
    //       PointLight point[MAX_POINT];
    //       SpotLight spot[MAX_SPOT];
    //   } u_lights;
    //
    // The three scalar counts plus padding fill the ivec4 so the light
    // arrays start on a 16-byte boundary, as std140 requires.
    struct gpu_lights
    {
        float ambient[4];

        int32_t directional_count;
        int32_t point_count;
        int32_t spot_count;
        int32_t pad0;

        gpu_directional_light directional[max_directional_lights];
        gpu_point_light point[max_point_lights];
        gpu_spot_light spot[max_spot_lights];
    };

    // Accumulate @p lights into @p out: ambient lights sum into a single
    // term, directional / point lights fill their arrays up to capacity,
    // and the counts are written. @p out is fully overwritten.
    void pack_lights(std::span<const light_proxy* const> lights, gpu_lights& out);
} // namespace rendering_engine
