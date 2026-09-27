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
