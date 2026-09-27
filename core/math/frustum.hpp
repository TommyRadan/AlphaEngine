// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/aabb.hpp>
#include <core/math/mat4.hpp>
#include <core/math/sphere.hpp>
#include <core/math/vec4.hpp>

namespace core::math
{
    /**
     * @brief View frustum represented by six planes in world space.
     *
     * Each plane is stored as @c vec4(n.x, n.y, n.z, d) with the
     * normal pointing into the visible volume; a point @c p is on
     * the inside of a plane when @c dot(n, p) + d >= 0. Planes are
     * normalized so signed-distance tests are valid.
     *
     * The @c near_p / @c far_p suffixes avoid the @c near / @c far
     * macros that some Windows headers still define.
     */
    struct frustum
    {
        enum plane_index
        {
            left = 0,
            right = 1,
            bottom = 2,
            top = 3,
            near_p = 4,
            far_p = 5,
            count = 6
        };

        vec4 planes[count]{};

        /**
         * @brief Extract a frustum from a view-projection matrix with the
         *        engine's [0, w] clip-space depth (see @ref perspective)
         *        using the Gribb/Hartmann method (planes in world space).
         */
        static frustum from_view_projection(const mat4& view_projection) noexcept;

        bool intersects(const aabb& box) const noexcept;
        bool intersects(const sphere& s) const noexcept;
    };
} // namespace core::math
