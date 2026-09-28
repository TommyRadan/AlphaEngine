// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/line_helper.hpp>

#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderer.hpp>

namespace rendering_engine::debug_draw
{
    line_helper::line_helper(renderer& owner, const char* name)
        : helper(owner, name), m_line(owner.device(), &owner.get_debug_line_material()), m_world(&owner.world())
    {
        // Every gizmo is a list of independent segments (vertex pairs).
        m_line.set_mode(line_mode::segments);

        m_placed_version = transform.get_world_version();
        m_proxy = m_world->create_mesh(describe(), transform.get_world_matrix());
    }

    line_helper::~line_helper()
    {
        m_world->destroy_mesh(m_proxy);
    }

    mesh_description line_helper::describe() const
    {
        // Debug gizmos are editor-only geometry: they stay on the default
        // camera mask (layer_all includes layer_editor) so nothing changes
        // visually, but a game can build a camera that clears the editor
        // bit to hide them from gameplay views. The overlay pass draws
        // them on top, never culled and never casting a shadow.
        mesh_description description = m_line.describe();
        description.bounds.reset();
        description.layer_mask = layer_editor;
        description.casts_shadow = false;
        description.overlay = true;
        description.name = name();
        return description;
    }

    void line_helper::set_segments(const std::vector<core::math::vec3>& positions,
                                   const std::vector<core::math::vec3>& colors)
    {
        m_line.set_positions(positions, colors);
        m_world->set_mesh_source(m_proxy, describe());
    }

    void line_helper::refresh() {}

    void line_helper::set_visible(bool visible)
    {
        helper::set_visible(visible);
        m_world->set_mesh_visible(m_proxy, visible);
    }

    void line_helper::update()
    {
        if (!is_visible())
        {
            return;
        }

        // Let dynamic gizmos follow their target.
        refresh();

        // Place the (mostly origin-baked) geometry; helpers that bake
        // world-space vertices leave the transform at identity.
        const uint64_t version = transform.get_world_version();
        if (version != m_placed_version)
        {
            m_world->set_mesh_world(m_proxy, transform.get_world_matrix());
            m_placed_version = version;
        }
    }
} // namespace rendering_engine::debug_draw
