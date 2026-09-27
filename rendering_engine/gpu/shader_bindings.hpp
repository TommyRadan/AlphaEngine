// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
 * per-frame set and number their few resources locally from 0. The one
 * exception is the volumetric fog raymarch, which binds the per-frame
 * set for the view, the lights and the shadow maps, so its own set's
 * numbers come from this table too.
 */

#pragma once

#include <cstdint>

namespace rendering_engine::gpu::shader_bindings
{
    // Set 0, owned by the scene pass: the per-view view_globals block
    // (camera matrices, viewport, clock, jitter and fog), the packed
    // lights, the directional and omni shadow data and their depth maps
    // (the omni map is one depth cube, sampled as a samplerCube).
    constexpr uint32_t per_frame = 0;
    constexpr uint32_t lights = 2;
    constexpr uint32_t shadow_map = 9;
    constexpr uint32_t shadow = 10;
    constexpr uint32_t point_shadow = 14;
    constexpr uint32_t point_shadow_map = 15;

    // Set 1, per draw: the per-draw model matrix, a slot of the per-draw
    // ring read at a dynamic offset. Every 3D renderable's per-draw group
    // binds it at this number, so the depth-only shadow pipelines reuse
    // those groups (and offsets) unchanged. Only on a device without push
    // constants (OpenGL): with them the block is the pipeline's push
    // constants and the binding stays in the layouts unread (see
    // include/per_draw.glsl).
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

    // Set 2: the ambient-occlusion map, which doubles as the packed
    // occlusion / roughness / metallic (ORM) map. 14-20 are the omni
    // shadow resources above, so this is the next free number.
    constexpr uint32_t material_occlusion_map = 21;

    // Set 0 as well: the spot-light shadow data (the first shadowing
    // spot light's perspective map). 9-21 are spent by the shadow /
    // material resources above, so this resumes at 22.
    constexpr uint32_t spot_shadow = 22;
    constexpr uint32_t spot_shadow_map = 23;

    // Set 1, in the per-draw group of a SKINNED variant only: the joint-
    // matrix storage buffer the vertex stage skins with (one mat4 per
    // palette entry, std430). A storage buffer rather than a UBO so the
    // palette has no fixed joint cap. 22 and 23 are spent by the spot
    // shadow resources above, so this resumes at 24.
    constexpr uint32_t per_draw_joints = 24;

    // Set 1 of the volumetric fog raymarch, whose set 0 is the scene
    // pass's per-frame group: its parameters block and the scene depth
    // it reconstructs world positions from. 24 is spent by the joint
    // palette above, so these resume at 25.
    constexpr uint32_t volumetric_fog_params = 25;
    constexpr uint32_t volumetric_fog_depth = 26;
} // namespace rendering_engine::gpu::shader_bindings
