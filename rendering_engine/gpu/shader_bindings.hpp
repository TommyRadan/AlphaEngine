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
 * @file shader_bindings.hpp
 * @brief The global binding-number table shared by every scene pipeline.
 *
 * The OpenGL backend consumes SPIR-V through ARB_gl_spirv, which
 * flattens the @c layout(set, binding) pairs of a pipeline into one
 * global namespace per resource class (UBOs, samplers). Every resource a
 * scene pipeline can see therefore needs a binding number that is unique
 * across all of its descriptor sets, not just within one. Vulkan only
 * requires uniqueness within a set, so the same numbering satisfies both
 * backends.
 *
 * This header is the single owner of that table. The build generates
 * @c shaders/include/bindings.glsl from it (see
 * @c cmake/generate_shader_bindings.cmake): each constant below becomes
 * a @c BINDING_<NAME> macro, so a shader spells
 * @c layout(set = 0, binding = BINDING_PER_FRAME) and the C++ side spells
 * @c shader_bindings::per_frame, and the two can never drift apart.
 *
 * Only lines of the exact form @c constexpr uint32_t name = N; are
 * exported, so keep the table to plain constants.
 *
 * Pass-private pipelines (the depth-only shadow passes, the fullscreen
 * post chain, the IBL compute kernels) bind nothing from the scene's
 * per-frame set and number their few resources locally from 0.
 */

#pragma once

#include <cstdint>

namespace rendering_engine::gpu::shader_bindings
{
    // Set 0, owned by the scene pass: the per-view block (camera
    // matrices + fog), the packed lights, the directional and omni
    // shadow data and their depth maps.
    constexpr uint32_t per_frame = 0;
    constexpr uint32_t lights = 2;
    constexpr uint32_t shadow_map = 9;
    constexpr uint32_t shadow = 10;
    constexpr uint32_t point_shadow = 14;
    constexpr uint32_t point_shadow_map_0 = 15;
    constexpr uint32_t point_shadow_map_1 = 16;
    constexpr uint32_t point_shadow_map_2 = 17;
    constexpr uint32_t point_shadow_map_3 = 18;
    constexpr uint32_t point_shadow_map_4 = 19;
    constexpr uint32_t point_shadow_map_5 = 20;

    // Set 1, owned by each renderable: the per-draw model matrix. Every
    // 3D renderable builds its per-draw bind group against this number,
    // so the depth-only shadow pipelines reuse those groups unchanged.
    constexpr uint32_t per_draw_model = 1;

    // Set 2, owned by each material: the params block and up to five
    // sampled maps. Materials with a single map (phong's diffuse, points'
    // sprite) take the albedo slot.
    constexpr uint32_t material_params = 3;
    constexpr uint32_t material_albedo_map = 4;
    constexpr uint32_t material_normal_map = 5;
    constexpr uint32_t material_metalness_map = 6;
    constexpr uint32_t material_roughness_map = 7;
    constexpr uint32_t material_emissive_map = 8;

    // Set 2 as well: the image-based-lighting tables standard_material
    // samples. 9 and 10 are spent by the scene pass's shadow resources
    // above, so these resume at 11.
    constexpr uint32_t material_irradiance_map = 11;
    constexpr uint32_t material_prefiltered_map = 12;
    constexpr uint32_t material_brdf_lut = 13;
} // namespace rendering_engine::gpu::shader_bindings
