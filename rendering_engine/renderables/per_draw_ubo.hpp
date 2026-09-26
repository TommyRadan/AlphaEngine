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
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <core/math/mat4.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

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

    // Determinant of @p model's upper-left 3x3: negative when the
    // transform mirrors (an odd number of negative scale axes).
    float model_determinant(const core::math::mat4& model);

    // Whether @p model flips handedness and so reverses triangle winding.
    bool is_mirrored(const core::math::mat4& model);

    // The block for @p model. The normal matrix is the inverse-transpose
    // of the upper-left 3x3; a singular 3x3 (a zero scale axis) falls
    // back to the model's own 3x3 so the upload never carries NaNs.
    per_draw_payload make_per_draw_payload(const core::math::mat4& model);

    // A dynamic uniform buffer sized for the block.
    gpu::buffer create_per_draw_ubo(gpu::device& device);

    // A bind group over @p ubo against @p layout at the shared
    // @c shader_bindings::per_draw_model binding.
    gpu::bind_group create_per_draw_bind_group(gpu::device& device, gpu::bind_group_layout layout, gpu::buffer ubo);

    // Upload the block for @p model into @p ubo; returns
    // @ref is_mirrored(model) so the caller can flag its draw item.
    bool write_per_draw_ubo(gpu::device& device, gpu::buffer ubo, const core::math::mat4& model);
} // namespace rendering_engine
