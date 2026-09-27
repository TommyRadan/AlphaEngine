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
 * @file per_draw_ubo.hpp
 * @brief The @c PerDraw block every 3D renderable uploads (shaders/include/
 *        per_draw.glsl) and the helpers that build, allocate and bind it.
 *
 * The block carries the model matrix and its normal matrix (the inverse-
 * transpose of the model's upper-left 3x3), computed once per draw here
 * so no vertex shader runs an @c inverse() per vertex. The same pass
 * over the matrix also tells whether the transform mirrors — a negative
 * determinant reverses every triangle's winding — which the renderable
 * records in @ref draw_item::mirrored so the pass can draw it with the
 * material's clockwise-front-face pipeline variant instead of culling
 * its outside.
 *
 * On a device with push constants (Vulkan) the block is the pipeline's
 * push constants: a renderable hands the pass its cached block
 * (@ref draw_item::per_draw_push) and the pass pushes it right before
 * the draw (@ref bind_per_draw), so no buffer, descriptor set or upload
 * is involved. Every material template's pipelines and the single-draw
 * shadow pipelines declare @ref per_draw_push_constant_range for it, and
 * the shaders read the block from push constants under the
 * @c AE_PUSH_CONSTANTS backend define (shaders/include/per_draw.glsl).
 *
 * Without push constants (OpenGL) rigid renderables still own no buffer
 * for the block: they write it into the frame's slice of the shared
 * @ref per_draw_ring and bind the ring's group with the slot's dynamic
 * offset, which is why the block's layout entry
 * (@ref per_draw_model_layout_entry) is dynamic.
 *
 * A skinned draw's per-draw group also carries its own joint palette, so
 * it cannot be the ring's shared one and keeps the block in a private
 * uniform buffer. Where the device takes push constants the skinned draw
 * pushes its block like any other and binds the group for the palette
 * alone.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <core/math/mat4.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
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

    // std140 layout of the PerDraw block: mat4 modelMatrix at 0, mat4
    // normalMatrix at 64 (the 3x3 in its upper-left, identity elsewhere,
    // widened so the upload is a plain copy). 128 bytes.
    struct per_draw_payload
    {
        core::math::mat4 model;
        core::math::mat4 normal;
    };

    constexpr size_t per_draw_ubo_size = sizeof(per_draw_payload);
    static_assert(per_draw_ubo_size == 128, "PerDraw block must be two std140 mat4s");
    static_assert(per_draw_ubo_size <= gpu::min_push_constants_size,
                  "PerDraw block must fit the push constants every device with them has");

    // Whether @p device takes the block as push constants: it has
    // @c device_features::push_constants, which guarantees room for it.
    // Fixed for the device's lifetime; it decides at once whether the
    // pipelines declare @ref per_draw_push_constant_range, whether the
    // shaders read the block from push constants (the device's library
    // modules are compiled with @c AE_PUSH_CONSTANTS) and whether the
    // renderables push it instead of taking a per-draw ring slot.
    bool per_draw_push_constants(const gpu::device& device);

    // The push-constant range the block occupies: offset 0, the whole
    // block, visible to the vertex and fragment stages like the uniform
    // block it replaces (the infinite grid reads the model matrix in its
    // fragment stage). On a device with push constants every material
    // template's pipelines declare it whether or not their shaders read
    // the block, so the pipelines a pass switches between keep the same
    // layout for the per-frame group it binds once; the single-draw
    // shadow pipelines declare it too.
    gpu::push_constant_range per_draw_push_constant_range();

    // Record @p item's per-draw data ahead of its draw: push its block
    // when it carries one (@ref draw_item::per_draw_push), then bind its
    // per-draw group, if any, at @p slot with its dynamic offsets. Every
    // pass that dispatches draw items calls this before each draw.
    void bind_per_draw(gpu::render_pass_encoder& encoder, const draw_item& item, uint32_t slot);

    // Determinant of @p model's upper-left 3x3: negative when the
    // transform mirrors (an odd number of negative scale axes).
    float model_determinant(const core::math::mat4& model);

    // Whether @p model flips handedness and so reverses triangle winding.
    bool is_mirrored(const core::math::mat4& model);

    // The block for @p model. The normal matrix is the inverse-transpose
    // of the upper-left 3x3; a singular 3x3 (a zero scale axis) falls
    // back to the model's own 3x3 so the upload never carries NaNs.
    per_draw_payload make_per_draw_payload(const core::math::mat4& model);

    // The PerDraw entry of a rigid per-draw layout: the block's uniform
    // buffer at @c shader_bindings::per_draw_model, taking a dynamic
    // offset so one group over the per-draw ring serves every draw. Every
    // layout a renderable's per-draw group is bound against (each 3D
    // material's slot 1 and the shadow passes' draw layouts) is built
    // from it, so the groups stay interchangeable between them.
    gpu::bind_group_layout_entry per_draw_model_layout_entry();

    // A dynamic uniform buffer sized for the block.
    gpu::buffer create_per_draw_ubo(gpu::device& device);

    // A bind group over @p ubo against @p layout at the shared
    // @c shader_bindings::per_draw_model binding, exposing one block
    // (@ref per_draw_ubo_size bytes) from the start of the buffer. Under
    // a dynamic layout entry each bind's offset then selects which block
    // of a larger buffer that is.
    gpu::bind_group create_per_draw_bind_group(gpu::device& device, gpu::bind_group_layout layout, gpu::buffer ubo);

    // Upload the block for @p model into @p ubo; returns
    // @ref is_mirrored(model) so the caller can flag its draw item.
    bool write_per_draw_ubo(gpu::device& device, gpu::buffer ubo, const core::math::mat4& model);

    // std430 stride of one joint-palette entry: a column-major mat4.
    constexpr size_t joint_matrix_size = sizeof(core::math::mat4);
    static_assert(joint_matrix_size == 64, "a joint matrix must be one std430 mat4");

    // A dynamic storage buffer for a skinned draw's joint palette with
    // room for @p joint_count matrices (at least one).
    gpu::buffer create_joint_buffer(gpu::device& device, size_t joint_count);

    // The per-draw bind group of a skinning variant: the PerDraw block
    // @p ubo at @c shader_bindings::per_draw_model plus the joint palette
    // @p joints at @c shader_bindings::per_draw_joints, against @p layout.
    gpu::bind_group create_skinned_per_draw_bind_group(gpu::device& device,
                                                       gpu::bind_group_layout layout,
                                                       gpu::buffer ubo,
                                                       gpu::buffer joints);

    // Upload @p matrices into the palette @p joints, which must have room
    // for all of them.
    void write_joint_buffer(gpu::device& device, gpu::buffer joints, std::span<const core::math::mat4> matrices);
} // namespace rendering_engine
