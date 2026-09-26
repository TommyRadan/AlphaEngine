/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
