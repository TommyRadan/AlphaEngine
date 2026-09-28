// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_2d/sprite_batch.hpp>

#include <cmath>

namespace rendering_engine
{
    sprite_batch::sprite_batch(ui_material* mat) : m_material{mat} {}

    void sprite_batch::clear()
    {
        for (ui_quad_group& group : m_groups)
        {
            group.vertices.clear();
        }
        m_quad_count = 0;
        changed();
    }

    ui_quad_group& sprite_batch::group_for(gpu::texture texture)
    {
        for (ui_quad_group& group : m_groups)
        {
            if (group.texture == texture)
            {
                return group;
            }
        }
        ui_quad_group& group = m_groups.emplace_back();
        group.texture = texture;
        return group;
    }

    void sprite_batch::add(gpu::texture texture, const sprite_quad& quad)
    {
        if (!texture.valid() && m_material != nullptr)
        {
            texture = m_material->white_texture();
        }
        ui_quad_group& group = group_for(texture);

        const core::math::vec2 rotation{std::cos(quad.rotation), std::sin(quad.rotation)};
        const auto corner = [&](bool max_x, bool max_y)
        {
            ui_vertex vertex{};
            vertex.pivot_anchor = quad.pivot_anchor;
            vertex.pivot_offset = quad.pivot_offset;
            vertex.corner_anchor = core::math::vec2{max_x ? quad.corner_max_anchor.x : quad.corner_min_anchor.x,
                                                    max_y ? quad.corner_max_anchor.y : quad.corner_min_anchor.y};
            vertex.corner_offset = core::math::vec2{max_x ? quad.corner_max_offset.x : quad.corner_min_offset.x,
                                                    max_y ? quad.corner_max_offset.y : quad.corner_min_offset.y};
            vertex.rotation = rotation;
            vertex.uv = core::math::vec2{max_x ? quad.uv_max.x : quad.uv_min.x, max_y ? quad.uv_max.y : quad.uv_min.y};
            vertex.color = quad.color;
            return vertex;
        };
        // Top-left, top-right, bottom-right, bottom-left; the renderer's
        // index buffer splits every quad along the 0-2 diagonal.
        group.vertices.push_back(corner(false, false));
        group.vertices.push_back(corner(true, false));
        group.vertices.push_back(corner(true, true));
        group.vertices.push_back(corner(false, true));
        ++m_quad_count;
        changed();
    }

    void sprite_batch::add(gpu::texture texture,
                           const rect_transform& rect,
                           const assets::color& color,
                           const core::math::vec2& uv_min,
                           const core::math::vec2& uv_max)
    {
        // Each corner of a rect_transform sits (t - pivot) * resolved size
        // from its pivot, t being 0 at the min edge and 1 at the max edge;
        // the resolved size is the anchor span times the drawable plus
        // rect.size, which splits into the anchor and pixel parts below.
        const core::math::vec2 span = rect.anchor_max - rect.anchor_min;
        const core::math::vec2 lead = -rect.pivot;
        const core::math::vec2 trail = core::math::vec2{1.0f, 1.0f} - rect.pivot;

        sprite_quad quad{};
        quad.pivot_anchor = rect.anchor_min + span * rect.pivot;
        quad.pivot_offset = rect.position;
        quad.corner_min_anchor = lead * span;
        quad.corner_min_offset = lead * rect.size;
        quad.corner_max_anchor = trail * span;
        quad.corner_max_offset = trail * rect.size;
        quad.rotation = rect.rotation;
        quad.uv_min = uv_min;
        quad.uv_max = uv_max;
        quad.color = color;
        add(texture, quad);
    }

    std::size_t sprite_batch::quad_count() const
    {
        return m_quad_count;
    }

    ui_element_data sprite_batch::capture()
    {
        // A texture nothing was queued for since the last clear has no
        // draw, and leaves the first-use order.
        std::erase_if(m_groups, [](const ui_quad_group& group) { return group.vertices.empty(); });

        ui_element_data data{};
        data.mat = m_material;
        data.groups = m_groups;
        return data;
    }
} // namespace rendering_engine
