// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/sphere.hpp>

#include <cmath>

namespace core::math
{
    bool sphere::contains(const vec3& point) const noexcept
    {
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        const float dz = point.z - center.z;
        return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
    }

    sphere merge(const sphere& a, const sphere& b) noexcept
    {
        const float dx = b.center.x - a.center.x;
        const float dy = b.center.y - a.center.y;
        const float dz = b.center.z - a.center.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;
        const float radius_diff = b.radius - a.radius;

        // One sphere already encloses the other; return the larger.
        if (radius_diff * radius_diff >= distance_sq)
        {
            return (a.radius >= b.radius) ? a : b;
        }

        const float distance = std::sqrt(distance_sq);
        const float new_radius = (distance + a.radius + b.radius) * 0.5f;
        const float t = (distance > 0.0f) ? ((new_radius - a.radius) / distance) : 0.0f;
        return sphere{vec3{a.center.x + dx * t, a.center.y + dy * t, a.center.z + dz * t}, new_radius};
    }

    sphere merge(const sphere& a, const vec3& point) noexcept
    {
        const float dx = point.x - a.center.x;
        const float dy = point.y - a.center.y;
        const float dz = point.z - a.center.z;
        const float distance_sq = dx * dx + dy * dy + dz * dz;

        // Point already inside.
        if (distance_sq <= a.radius * a.radius)
        {
            return a;
        }

        const float distance = std::sqrt(distance_sq);
        const float new_radius = (distance + a.radius) * 0.5f;
        const float t = (new_radius - a.radius) / distance;
        return sphere{vec3{a.center.x + dx * t, a.center.y + dy * t, a.center.z + dz * t}, new_radius};
    }
} // namespace core::math
