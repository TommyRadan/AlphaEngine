// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/point_light_helper.hpp>

#include <algorithm>
#include <array>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/lighting/point_light.hpp>

namespace rendering_engine::debug_draw
{
    namespace
    {
        namespace math = core::math;

        math::vec3 clamp_color(const math::vec3& c)
        {
            return math::vec3{std::clamp(c.x, 0.0f, 1.0f), std::clamp(c.y, 0.0f, 1.0f), std::clamp(c.z, 0.0f, 1.0f)};
        }
    } // namespace

    point_light_helper::point_light_helper(const point_light* light, float size)
        : line_helper("Point light"), m_light(light), m_size(size)
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
        // Octahedron: an apex pair along the engine up axis (+Z / -Z) over a
        // four-vertex equator ring in the horizontal X/Y plane, all offset
        // to the light position.
        const math::vec3 top = position + math::world_up * r;
        const math::vec3 bottom = position - math::world_up * r;
        const std::array<math::vec3, 4> ring{position + math::vec3{r, 0.0f, 0.0f},
                                             position + math::vec3{0.0f, r, 0.0f},
                                             position - math::vec3{r, 0.0f, 0.0f},
                                             position - math::vec3{0.0f, r, 0.0f}};

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
} // namespace rendering_engine::debug_draw
