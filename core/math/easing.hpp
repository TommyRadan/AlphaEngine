// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file easing.hpp
 * @brief Easing functions: remaps of a normalised progress value that shape
 *        how an interpolation accelerates and settles.
 */

#pragma once

#include <cstdint>

namespace core::math
{
    /**
     * @brief The easing curves @ref ease evaluates.
     *
     * Each family comes in three flavours: @c in_* starts slowly and
     * accelerates, @c out_* starts fast and decelerates into the target, and
     * @c in_out_* does both, symmetric about t = 0.5. The @c back family
     * overshoots (below 0 for @c in, above 1 for @c out) before settling; the
     * @c bounce family rebounds off the end value. Every curve maps 0 to 0
     * and 1 to 1.
     */
    enum class easing : uint8_t
    {
        linear,
        in_quad,
        out_quad,
        in_out_quad,
        in_cubic,
        out_cubic,
        in_out_cubic,
        in_sine,
        out_sine,
        in_out_sine,
        in_expo,
        out_expo,
        in_out_expo,
        in_back,
        out_back,
        in_out_back,
        in_bounce,
        out_bounce,
        in_out_bounce,
    };

    /**
     * @brief Evaluates @p kind at progress @p t.
     *
     * @p t is clamped to [0, 1] first, so a caller may pass a raw
     * elapsed / duration ratio. The result is the eased progress: in [0, 1]
     * for every family except @c back, which overshoots by design.
     */
    float ease(easing kind, float t) noexcept;
} // namespace core::math
