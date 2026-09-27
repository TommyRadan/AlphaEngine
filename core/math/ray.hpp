// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/vec3.hpp>

namespace core::math
{
    /**
     * @brief Half-line starting at @ref origin and running along
     *        @ref direction.
     *
     * The direction need not be unit length: @ref point_at scales it as
     * given, so a parameter reads as a distance only when the direction is
     * normalised. Queries that report a hit distance (the physics raycast)
     * normalise it themselves.
     */
    struct ray
    {
        vec3 origin{0.0f, 0.0f, 0.0f};
        vec3 direction{1.0f, 0.0f, 0.0f};

        constexpr ray() noexcept = default;
        constexpr ray(const vec3& in_origin, const vec3& in_direction) noexcept
            : origin{in_origin}, direction{in_direction}
        {
        }

        /** @brief The point @c origin + @p t * @c direction. */
        vec3 point_at(float t) const noexcept;
    };
} // namespace core::math
