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

#include <core/math/aabb.hpp>

#include <algorithm>
#include <cmath>

namespace core::math
{
    vec3 aabb::center() const noexcept
    {
        return vec3{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f};
    }

    vec3 aabb::extents() const noexcept
    {
        return vec3{(max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f};
    }

    bool aabb::contains(const vec3& point) const noexcept
    {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z &&
               point.z <= max.z;
    }

    aabb merge(const aabb& a, const aabb& b) noexcept
    {
        return aabb{vec3{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
                    vec3{std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
    }

    aabb merge(const aabb& a, const vec3& point) noexcept
    {
        return aabb{vec3{std::min(a.min.x, point.x), std::min(a.min.y, point.y), std::min(a.min.z, point.z)},
                    vec3{std::max(a.max.x, point.x), std::max(a.max.y, point.y), std::max(a.max.z, point.z)}};
    }

    aabb transform(const aabb& box, const mat4& m) noexcept
    {
        // Column-major storage: m.m[col * 4 + row]. The centre goes through
        // the full affine transform; each half-extent is the sum of the
        // input half-extents weighted by the magnitude of the matching row
        // of the linear part, which is exactly the extent the eight
        // transformed corners span.
        const vec3 center = box.center();
        const vec3 extents = box.extents();

        vec3 out_center;
        vec3 out_extents;
        for (int row = 0; row < 3; ++row)
        {
            const float mx = m.m[0 * 4 + row];
            const float my = m.m[1 * 4 + row];
            const float mz = m.m[2 * 4 + row];
            const float mw = m.m[3 * 4 + row];
            out_center.data()[row] = mx * center.x + my * center.y + mz * center.z + mw;
            out_extents.data()[row] = std::abs(mx) * extents.x + std::abs(my) * extents.y + std::abs(mz) * extents.z;
        }
        return aabb{out_center - out_extents, out_center + out_extents};
    }
} // namespace core::math
