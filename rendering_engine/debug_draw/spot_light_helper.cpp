// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/spot_light_helper.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/lighting/spot_light.hpp>

namespace rendering_engine::debug_draw
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

        // Points around the cone's base circle; four of them also draw a
        // spoke back to the apex so the gizmo reads as a cone rather than
        // a flat ring.
        constexpr int ring_segments = 16;
        constexpr int spoke_stride = ring_segments / 4;
    } // namespace

    spot_light_helper::spot_light_helper(renderer& owner, const spot_light* light, float size)
        : line_helper(owner, "Spot light"), m_light(light), m_size(size)
    {
    }

    void spot_light_helper::refresh()
    {
        if (m_light == nullptr)
        {
            return;
        }

        const math::vec3 position = m_light->position;
        const math::vec3 direction = m_light->direction;
        const math::vec3 color = m_light->color;
        const float outer_angle = m_light->outer_angle;
        if (m_built && position == m_last_position && direction == m_last_direction && color == m_last_color &&
            outer_angle == m_last_outer_angle)
        {
            return;
        }
        m_last_position = position;
        m_last_direction = direction;
        m_last_color = color;
        m_last_outer_angle = outer_angle;
        m_built = true;

        const math::vec3 dir = math::normalize(direction);
        // Build an orthonormal basis around the cone axis on the engine up
        // axis (+Z); reference_up swaps in a horizontal axis when the light
        // points straight up or down so the cross product is stable.
        const math::vec3 world_up = math::reference_up(dir);
        const math::vec3 right = math::normalize(math::cross(world_up, dir));
        const math::vec3 up = math::normalize(math::cross(dir, right));

        const math::vec3 apex = position;
        const math::vec3 base_center = apex + dir * m_size;
        const float radius = m_size * std::tan(outer_angle);

        std::vector<math::vec3> ring(ring_segments);
        for (int i = 0; i < ring_segments; ++i)
        {
            const float angle = 2.0f * 3.14159265359f * static_cast<float>(i) / static_cast<float>(ring_segments);
            ring[static_cast<size_t>(i)] =
                base_center + right * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
        }

        std::vector<math::vec3> positions;
        positions.reserve(static_cast<size_t>(ring_segments) * 2 + 8);
        for (int i = 0; i < ring_segments; ++i)
        {
            const math::vec3& a = ring[static_cast<size_t>(i)];
            const math::vec3& b = ring[static_cast<size_t>((i + 1) % ring_segments)];
            positions.push_back(a);
            positions.push_back(b);
            if (i % spoke_stride == 0)
            {
                positions.push_back(apex);
                positions.push_back(a);
            }
        }

        const math::vec3 rgb = clamp_color(color);
        std::vector<math::vec3> colors(positions.size(), rgb);
        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug_draw
