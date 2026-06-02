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
} // namespace core::math
