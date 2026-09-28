// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/line_helper.hpp>

#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

namespace rendering_engine::debug_draw
{
    line_helper::line_helper(renderer& owner, const char* name)
        : helper(owner, name), m_line(owner.device(), &owner.get_debug_line_material())
    {
        // Every gizmo is a list of independent segments (vertex pairs).
        m_line.set_mode(line_mode::segments);

        owner.register_debug_renderable(this);
    }

    line_helper::~line_helper()
    {
        owner().unregister_debug_renderable(this);
    }

    void line_helper::set_segments(const std::vector<core::math::vec3>& positions,
                                   const std::vector<core::math::vec3>& colors)
    {
        m_line.set_positions(positions, colors);
    }

    void line_helper::refresh() {}

    void line_helper::collect_draw_items(std::vector<draw_item>& out)
    {
        if (!is_visible())
        {
            return;
        }

        // Let dynamic gizmos follow their target before they draw.
        refresh();

        const mesh_description geometry = m_line.describe();
        if (geometry.mat == nullptr || geometry.mesh == nullptr || !geometry.mesh->vertex_buffer.valid())
        {
            return;
        }

        // The segments draw their vertices directly, two per segment (an odd
        // trailing vertex is dropped). The (mostly origin-baked) geometry is
        // placed by the helper's transform; helpers that bake world-space
        // vertices leave it at identity.
        draw_item item{};
        item.mat = geometry.mat;
        m_per_draw.bind(transform, item);
        item.vertex_buffer = geometry.mesh->vertex_buffer;
        item.vertex_stride = geometry.mesh->vertex_stride;
        item.vertex_count = geometry.vertex_count.value_or(geometry.mesh->vertex_count);
        out.push_back(item);
    }
} // namespace rendering_engine::debug_draw
