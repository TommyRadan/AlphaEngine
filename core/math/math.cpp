// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/math.hpp>

#include <cmath>

#include <glm/glm.hpp>

namespace core::math
{
    float lerp(float a, float b, float t) noexcept
    {
        return glm::mix(a, b, t);
    }

    vec3 reference_up(const vec3& direction, const vec3& up) noexcept
    {
        // Below this a vector carries no usable direction.
        constexpr float epsilon = 1e-6f;
        // |cos| above this (about 0.8 degrees off the axis) counts as parallel:
        // the cross product still has a direction but too little magnitude to
        // normalise into a stable frame.
        constexpr float parallel_cosine = 0.9999f;

        const float direction_length = length(direction);
        const float up_length = length(up);
        if (direction_length <= epsilon)
        {
            return up_length > epsilon ? up : world_up;
        }
        if (up_length > epsilon && std::abs(dot(direction, up)) < parallel_cosine * direction_length * up_length)
        {
            return up;
        }

        // Parallel or no reference: the engine forward (+X) stands in, unless
        // the direction itself runs (nearly) along +X, when +Y does. Either
        // way the substitute is well clear of the direction, so the frame
        // built from the pair is well-conditioned.
        const vec3 unit_direction = direction / direction_length;
        return std::abs(unit_direction.x) < parallel_cosine ? world_forward : vec3{0.0f, 1.0f, 0.0f};
    }
} // namespace core::math
