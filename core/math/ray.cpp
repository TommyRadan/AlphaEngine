// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/ray.hpp>

namespace core::math
{
    vec3 ray::point_at(float t) const noexcept
    {
        return vec3{origin.x + direction.x * t, origin.y + direction.y * t, origin.z + direction.z * t};
    }
} // namespace core::math
