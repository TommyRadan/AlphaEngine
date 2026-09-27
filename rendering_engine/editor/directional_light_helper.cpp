// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/editor/directional_light_helper.hpp>

#include <algorithm>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/lighting/directional_light.hpp>

namespace rendering_engine::editor
{
    namespace
    {
        namespace math = core::math;

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
        // Build an orthonormal basis around the travel direction on the
        // engine up axis (+Z); reference_up swaps in a horizontal axis when
        // the light points straight up or down so the cross product is
        // stable.
        const math::vec3 world_up = math::reference_up(dir);
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
} // namespace rendering_engine::editor
