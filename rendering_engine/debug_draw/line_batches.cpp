// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/line_batches.hpp>

#include <core/math/mat4.hpp>
#include <rendering_engine/debug_draw/draw_list.hpp>
#include <rendering_engine/material_library.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderables/line.hpp>

namespace rendering_engine::debug_draw
{
    line_batches::line_batches(render_world& world, gpu::device& device, material_library& materials)
        : m_world{&world}, m_device{&device}, m_materials{&materials}
    {
        m_on_top.mat = &materials.get_debug_line_material();
    }

    line_batches::~line_batches()
    {
        clear(m_on_top);
        clear(m_depth_tested);
    }

    void line_batches::capture()
    {
        capture(m_on_top, false);
        capture(m_depth_tested, true);
    }

    void line_batches::clear(batch& target)
    {
        if (target.proxy.valid())
        {
            m_world->destroy_mesh(target.proxy);
            target.proxy = {};
        }
        target.geometry.reset();
        target.positions.clear();
        target.colors.clear();
    }

    void line_batches::capture(batch& target, bool depth_test)
    {
        m_positions.clear();
        m_colors.clear();
        for (const line_segment& segment : m_world->debug_draw_list().lines(depth_test))
        {
            m_positions.push_back(segment.from);
            m_positions.push_back(segment.to);
            m_colors.push_back(segment.color);
            m_colors.push_back(segment.color);
        }

        if (m_positions.empty())
        {
            // Nothing to draw: no proxy, so the passes see no draw at all.
            clear(target);
            return;
        }
        if (target.proxy.valid() && m_positions == target.positions && m_colors == target.colors)
        {
            return;
        }

        if (target.mat == nullptr)
        {
            // Tested against the scene depth but not writing it, so the
            // lines never hide one another or what the scene draws later.
            m_depth_tested_material = m_materials->create_line_material(true);
            m_depth_tested_material->set_depth_write(false);
            target.mat = m_depth_tested_material.get();
        }
        if (target.geometry == nullptr)
        {
            target.geometry = std::make_unique<rendering_engine::line>(*m_device, target.mat);
            target.geometry->set_mode(line_mode::segments);
        }
        target.geometry->set_positions(m_positions, m_colors);
        target.positions = m_positions;
        target.colors = m_colors;

        // World-space segments at the identity placement, on the editor
        // layer (a gameplay camera that clears that bit hides them), never
        // culled and never casting a shadow.
        mesh_description description = target.geometry->describe();
        description.bounds.reset();
        description.layer_mask = layer_editor;
        description.casts_shadow = false;
        description.overlay = !depth_test;
        description.name = depth_test ? "debug_draw (depth-tested)" : "debug_draw";
        if (target.proxy.valid())
        {
            m_world->set_mesh_source(target.proxy, description);
        }
        else
        {
            target.proxy = m_world->create_mesh(description, core::math::mat4{});
        }
    }
} // namespace rendering_engine::debug_draw
