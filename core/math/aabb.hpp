// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>

namespace core::math
{
    /** @brief Axis-aligned bounding box defined by two corner points. */
    struct aabb
    {
        vec3 min{0.0f, 0.0f, 0.0f};
        vec3 max{0.0f, 0.0f, 0.0f};

        constexpr aabb() noexcept = default;
        constexpr aabb(const vec3& in_min, const vec3& in_max) noexcept : min{in_min}, max{in_max} {}

        vec3 center() const noexcept;
        vec3 extents() const noexcept;
        bool contains(const vec3& point) const noexcept;
    };

    aabb merge(const aabb& a, const aabb& b) noexcept;
    aabb merge(const aabb& a, const vec3& point) noexcept;

    /**
     * @brief Axis-aligned box enclosing @p box after the affine transform
     *        @p m (translation, rotation, non-uniform scale, shear).
     *
     * Equivalent to transforming the eight corners of @p box and re-boxing
     * them, computed without enumerating the corners: the centre maps
     * through @p m and each output half-extent is the absolute 3x3 part of
     * @p m applied to the input half-extents. The bottom row of @p m is
     * ignored, so a projective matrix is not supported.
     */
    aabb transform(const aabb& box, const mat4& m) noexcept;
} // namespace core::math
