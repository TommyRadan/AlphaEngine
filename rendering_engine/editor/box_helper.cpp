// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/editor/box_helper.hpp>

#include <array>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/editor/box_edges.hpp>

namespace rendering_engine::editor
{
    box_helper::box_helper(const core::math::aabb& box, color color) : line_helper("Box"), m_box(box), m_color(color)
    {
        rebuild();
    }

    void box_helper::set_box(const core::math::aabb& box)
    {
        m_box = box;
        rebuild();
    }

    void box_helper::rebuild()
    {
        namespace math = core::math;

        // The eight corners of the box, indexed so bit 0 selects X,
        // bit 1 selects Y and bit 2 selects Z between min and max.
        const std::array<math::vec3, 8> corners{math::vec3{m_box.min.x, m_box.min.y, m_box.min.z},
                                                math::vec3{m_box.max.x, m_box.min.y, m_box.min.z},
                                                math::vec3{m_box.min.x, m_box.max.y, m_box.min.z},
                                                math::vec3{m_box.max.x, m_box.max.y, m_box.min.z},
                                                math::vec3{m_box.min.x, m_box.min.y, m_box.max.z},
                                                math::vec3{m_box.max.x, m_box.min.y, m_box.max.z},
                                                math::vec3{m_box.min.x, m_box.max.y, m_box.max.z},
                                                math::vec3{m_box.max.x, m_box.max.y, m_box.max.z}};

        std::vector<math::vec3> positions;
        std::vector<math::vec3> colors;
        build_box_edges(corners, to_rgb(m_color), positions, colors);
        set_segments(positions, colors);
    }
} // namespace rendering_engine::editor
