// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file constants.hpp
 * @brief The angle constants every module shares, as plain @c float
 *        values with no GLM behind them.
 */

#pragma once

namespace core::math
{
    /** @brief Pi, the half turn in radians, as the nearest @c float. */
    inline constexpr float pi = 3.14159265358979323846f;

    /** @brief The full turn in radians, @c 2 * pi (exact: doubling a @c float does not round). */
    inline constexpr float two_pi = 2.0f * pi;

    /** @brief The quarter turn in radians, @c pi / 2 (exact: halving a @c float does not round). */
    inline constexpr float half_pi = 0.5f * pi;
} // namespace core::math
