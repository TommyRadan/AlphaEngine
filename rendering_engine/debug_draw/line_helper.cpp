// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/line_helper.hpp>

#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/renderer.hpp>

namespace rendering_engine::debug_draw
{
    line_helper::line_helper(renderer& owner, const char* name)
        : helper(owner, name, helper_layer::overlay), m_line(owner.device(), &owner.get_debug_line_material())
    {
        // Every gizmo is a list of independent segments (vertex pairs).
        m_line.set_mode(line_mode::segments);
    }

    line_helper::~line_helper() = default;

    void line_helper::upload()
    {
        // Geometry is uploaded eagerly in set_segments(); nothing to do
        // when the pass requests an upload.
    }

    void line_helper::set_segments(const std::vector<core::math::vec3>& positions,
                                   const std::vector<core::math::vec3>& colors)
    {
        m_line.set_positions(positions, colors);
        m_line.upload();
    }

    void line_helper::refresh() {}

    void line_helper::collect_draw_items(std::vector<draw_item>& out)
    {
        if (!visible)
        {
            return;
        }

        // Let dynamic gizmos follow their target before they draw.
        refresh();

        // Place the (mostly origin-baked) geometry; helpers that bake
        // world-space vertices leave the transform at identity.
        m_line.transform = transform;
        m_line.collect_draw_items(out);
    }
} // namespace rendering_engine::debug_draw
