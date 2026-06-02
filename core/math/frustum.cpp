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

#include <core/math/frustum.hpp>

#include <cmath>

namespace core::math
{
    namespace
    {
        // Read row r of a column-major mat4 (m[col*4 + row]).
        vec4 row_of(const mat4& m, int r) noexcept
        {
            return vec4{m.m[0 * 4 + r], m.m[1 * 4 + r], m.m[2 * 4 + r], m.m[3 * 4 + r]};
        }

        vec4 normalize_plane(const vec4& p) noexcept
        {
            const float length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (length <= 0.0f)
            {
                return p;
            }
            const float inv = 1.0f / length;
            return vec4{p.x * inv, p.y * inv, p.z * inv, p.w * inv};
        }
    } // namespace

    frustum frustum::from_view_projection(const mat4& view_projection) noexcept
    {
        const vec4 r0 = row_of(view_projection, 0);
        const vec4 r1 = row_of(view_projection, 1);
        const vec4 r2 = row_of(view_projection, 2);
        const vec4 r3 = row_of(view_projection, 3);

        frustum f;
        f.planes[left] = normalize_plane(vec4{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w});
        f.planes[right] = normalize_plane(vec4{r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w});
        f.planes[bottom] = normalize_plane(vec4{r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w});
        f.planes[top] = normalize_plane(vec4{r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w});
        f.planes[near_p] = normalize_plane(vec4{r3.x + r2.x, r3.y + r2.y, r3.z + r2.z, r3.w + r2.w});
        f.planes[far_p] = normalize_plane(vec4{r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w});
        return f;
    }

    bool frustum::intersects(const aabb& box) const noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const vec4& p = planes[i];
            // Positive vertex: corner of the box farthest along the plane normal.
            const float px = (p.x >= 0.0f) ? box.max.x : box.min.x;
            const float py = (p.y >= 0.0f) ? box.max.y : box.min.y;
            const float pz = (p.z >= 0.0f) ? box.max.z : box.min.z;
            if (p.x * px + p.y * py + p.z * pz + p.w < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool frustum::intersects(const sphere& s) const noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const vec4& p = planes[i];
            const float distance = p.x * s.center.x + p.y * s.center.y + p.z * s.center.z + p.w;
            if (distance < -s.radius)
            {
                return false;
            }
        }
        return true;
    }
} // namespace core::math
