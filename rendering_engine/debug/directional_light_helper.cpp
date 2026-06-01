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

#include <rendering_engine/debug/directional_light_helper.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/directional_light.hpp>

namespace rendering_engine::debug
{
    namespace
    {
        namespace math = infrastructure::math;

        // The light radiance is unbounded (colour * intensity), so clamp
        // each channel into the displayable [0, 1] before it becomes the
        // gizmo tint.
        math::vec3 clamp_color(const math::vec3& c)
        {
            return math::vec3{std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f)};
        }
    } // namespace

    directional_light_helper::directional_light_helper(const directional_light* light, float size)
        : line_helper("Directional light"), m_light(light), m_size(size)
    {
    }

    void directional_light_helper::refresh()
    {
        if (m_light == nullptr)
        {
            return;
        }

        const math::vec3 direction = m_light->direction;
        const math::vec3 color = m_light->color;
        if (m_built && direction == m_last_direction && color == m_last_color)
        {
            return;
        }
        m_last_direction = direction;
        m_last_color = color;
        m_built = true;

        const math::vec3 dir = math::normalize(direction);
        // Build an orthonormal basis around the travel direction; pick a
        // world up that is not parallel to it so the cross product is
        // stable.
        const math::vec3 world_up =
            std::abs(dir.y) < 0.99f ? math::vec3{0.0f, 1.0f, 0.0f} : math::vec3{1.0f, 0.0f, 0.0f};
        const math::vec3 right = math::normalize(math::cross(world_up, dir));
        const math::vec3 up = math::normalize(math::cross(dir, right));

        const math::vec3 origin{0.0f, 0.0f, 0.0f};
        const float hs = m_size * 0.25f;
        // Square panel facing the light direction, centred at the origin.
        const math::vec3 c0 = origin + right * hs + up * hs;
        const math::vec3 c1 = origin - right * hs + up * hs;
        const math::vec3 c2 = origin - right * hs - up * hs;
        const math::vec3 c3 = origin + right * hs - up * hs;

        const math::vec3 rgb = clamp_color(color);
        std::vector<math::vec3> positions{c0, c1, c1, c2, c2, c3, c3, c0, origin, origin + dir * m_size};
        std::vector<math::vec3> colors(positions.size(), rgb);

        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug
