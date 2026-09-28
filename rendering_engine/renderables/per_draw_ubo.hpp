// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file per_draw_ubo.hpp
 * @brief The @c PerDraw block every placed 3D draw hands its pass
 *        (shaders/include/per_draw.glsl) and the helpers that build and
 *        record it.
 *
 * The block carries the model matrix and its normal matrix (the inverse-
 * transpose of the model's upper-left 3x3), computed on the CPU here
 * so no vertex shader runs an @c inverse() per vertex. The same pass
 * over the matrix also tells whether the transform mirrors — a negative
 * determinant reverses every triangle's winding — which a mesh proxy
 * records, for @ref draw_item::mirrored, so the pass can draw it with the
 * material's clockwise-front-face pipeline variant instead of culling
 * its outside.
 *
 * The block is the pipeline's push constants: a mesh proxy carries the
 * block built from its world matrix (@ref mesh_proxy::per_draw), each
 * draw points at it (@ref draw_item::per_draw_push) and the pass
 * pushes it right before the draw (@ref bind_per_draw), so no buffer,
 * descriptor set or upload is involved. Every material template's
 * pipelines and the single-draw shadow pipelines declare
 * @ref per_draw_push_constant_range for it.
 *
 * A skinned draw also binds a per-draw group of its own at the
 * material's per-draw slot, carrying its joint palette.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <core/math/mat4.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
        struct render_pass_encoder;
    } // namespace gpu
    struct draw_item;

    // Layout of the PerDraw block: mat4 modelMatrix at 0, mat4
    // normalMatrix at 64 (the 3x3 in its upper-left, identity elsewhere,
    // widened so the push is a plain copy). 128 bytes.
    struct per_draw_payload
    {
        core::math::mat4 model;
        core::math::mat4 normal;
    };

    constexpr size_t per_draw_ubo_size = sizeof(per_draw_payload);
    static_assert(per_draw_ubo_size == 128, "PerDraw block must be two mat4s");
    static_assert(per_draw_ubo_size <= gpu::min_push_constants_size,
                  "PerDraw block must fit the push constants every device has");

    // The push-constant range the block occupies: offset 0, the whole
    // block, visible to the vertex and fragment stages (the infinite grid
    // reads the model matrix in its fragment stage). Every material
    // template's pipelines declare it whether or not their shaders read
    // the block, so the pipelines a pass switches between keep the same
    // layout for the per-frame group it binds once; the single-draw
    // shadow pipelines declare it too.
    gpu::push_constant_range per_draw_push_constant_range();

    // Push @p item's block (@ref draw_item::per_draw_push), if it carries
    // one, for the draw recorded next.
    void push_per_draw(gpu::render_pass_encoder& encoder, const draw_item& item);

    // Record @p item's per-draw data ahead of its draw: push its block
    // (@ref push_per_draw), then bind its per-draw group, if any, at
    // @p slot. Every pass that dispatches material draw items calls this
    // before each draw.
    void bind_per_draw(gpu::render_pass_encoder& encoder, const draw_item& item, uint32_t slot);

    // Determinant of @p model's upper-left 3x3: negative when the
    // transform mirrors (an odd number of negative scale axes).
    float model_determinant(const core::math::mat4& model);

    // Whether @p model flips handedness and so reverses triangle winding.
    bool is_mirrored(const core::math::mat4& model);

    // The block for @p model. The normal matrix is the inverse-transpose
    // of the upper-left 3x3; a singular 3x3 (a zero scale axis) falls
    // back to the model's own 3x3 so the push never carries NaNs.
    per_draw_payload make_per_draw_payload(const core::math::mat4& model);

    // std430 stride of one joint-palette entry: a column-major mat4.
    constexpr size_t joint_matrix_size = sizeof(core::math::mat4);
    static_assert(joint_matrix_size == 64, "a joint matrix must be one std430 mat4");

    // A dynamic storage buffer for a skinned draw's joint palette with
    // room for @p joint_count matrices (at least one).
    gpu::buffer create_joint_buffer(gpu::device& device, size_t joint_count);

    // The per-draw bind group of a skinning variant: the joint palette
    // @p joints at @c shader_bindings::per_draw_joints, against @p layout.
    gpu::bind_group
    create_skinned_per_draw_bind_group(gpu::device& device, gpu::bind_group_layout layout, gpu::buffer joints);

    // Upload @p matrices into the palette @p joints, which must have room
    // for all of them.
    void write_joint_buffer(gpu::device& device, gpu::buffer joints, std::span<const core::math::mat4> matrices);
} // namespace rendering_engine
