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

#include <rendering_engine/debug/point_light_helper.hpp>

#include <algorithm>
#include <array>
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/point_light.hpp>

namespace rendering_engine::debug
{
    namespace
    {
        namespace math = infrastructure::math;

        math::vec3 clamp_color(const math::vec3& c)
        {
            return math::vec3{std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f)};
        }
    } // namespace

    point_light_helper::point_light_helper(const point_light* light, float size)
        : helper("Point light"), m_light(light), m_size(size)
    {
    }

    void point_light_helper::refresh()
    {
        if (m_light == nullptr)
        {
            return;
        }

        const math::vec3 position = m_light->position;
        const math::vec3 color = m_light->color;
        if (m_built && position == m_last_position && color == m_last_color)
        {
            return;
        }
        m_last_position = position;
        m_last_color = color;
        m_built = true;

        const float r = m_size;
        // Octahedron: a +Y / -Y apex pair over a four-vertex equator ring
        // in the X/Z plane, all offset to the light position.
        const math::vec3 top = position + math::vec3{0.0f, r, 0.0f};
        const math::vec3 bottom = position - math::vec3{0.0f, r, 0.0f};
        const std::array<math::vec3, 4> ring{position + math::vec3{r, 0.0f, 0.0f},
                                             position + math::vec3{0.0f, 0.0f, r},
                                             position - math::vec3{r, 0.0f, 0.0f},
                                             position - math::vec3{0.0f, 0.0f, r}};

        std::vector<math::vec3> positions;
        positions.reserve(24);
        for (size_t i = 0; i < ring.size(); ++i)
        {
            const math::vec3& a = ring[i];
            const math::vec3& b = ring[(i + 1) % ring.size()];
            // Equator edge plus the two edges up to the apexes.
            positions.push_back(a);
            positions.push_back(b);
            positions.push_back(a);
            positions.push_back(top);
            positions.push_back(a);
            positions.push_back(bottom);
        }

        const math::vec3 rgb = clamp_color(color);
        std::vector<math::vec3> colors(positions.size(), rgb);
        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug
