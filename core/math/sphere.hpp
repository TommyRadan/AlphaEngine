// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/vec3.hpp>

namespace core::math
{
    /** @brief Bounding sphere defined by a center point and a radius. */
    struct sphere
    {
        vec3 center{0.0f, 0.0f, 0.0f};
        float radius{0.0f};

        constexpr sphere() noexcept = default;
        constexpr sphere(const vec3& in_center, float in_radius) noexcept : center{in_center}, radius{in_radius} {}

        bool contains(const vec3& point) const noexcept;
    };

    sphere merge(const sphere& a, const sphere& b) noexcept;
    sphere merge(const sphere& a, const vec3& point) noexcept;
} // namespace core::math
