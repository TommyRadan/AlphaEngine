// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file projection_jitter.hpp
 * @brief The temporal-AA sub-pixel jitter: its sequence and how it is
 *        applied to (and undone from) a projection.
 *
 * Device-free so the maths is unit-testable. The renderer computes
 * one jitter per frame from the live target size and publishes it through
 * @ref frame_context::jitter; the scene and skybox passes apply it with
 * @ref jitter_projection so their depth and colour agree, and the velocity
 * pass subtracts it again when it reconstructs each pixel's true position.
 */

#pragma once

#include <cstdint>

#include <core/math/math.hpp>

namespace rendering_engine
{
    // Length of the Halton sub-pixel jitter sequence the temporal-AA path
    // cycles through. Sixteen distinct offsets give the accumulation enough
    // sample positions to resolve cleanly without the pattern repeating
    // often enough to be visible.
    constexpr uint32_t taa_jitter_period = 16;

    // Halton low-discrepancy sequence: the standard source of well-spread
    // sub-pixel offsets. @p index is 1-based (index 0 degenerates to 0);
    // base 2 drives the x offset, base 3 the y.
    float halton(uint32_t index, uint32_t base);

    // Sub-pixel jitter for @p frame_index, in NDC units, sized for a
    // @p width x @p height target: each component lies within
    // (-1/width, 1/width) resp. (-1/height, 1/height), i.e. within half a
    // pixel of the pixel centre (NDC spans two units across the target).
    // The sequence repeats every @ref taa_jitter_period frames. A zero
    // dimension yields a zero jitter, so a degenerate target renders
    // unjittered.
    core::math::vec2 taa_jitter_ndc(uint64_t frame_index, uint32_t width, uint32_t height);

    // Offsets @p projection so every projected point lands @p jitter_ndc
    // further along x / y after the perspective divide. Implemented as a
    // clip-space translation post-multiplied onto the projection (each of
    // the first two rows gains @c jitter * row 3), so the shift is uniform
    // in NDC for perspective and orthographic projections alike. The
    // inverse of the result maps a jittered NDC point back to the world
    // position that was rasterised there.
    core::math::mat4 jitter_projection(const core::math::mat4& projection, const core::math::vec2& jitter_ndc);
} // namespace rendering_engine
