// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/circle.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/mesh_bounds.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/vertex_format_check.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/cache_key.hpp>
#include <runtime/engine.hpp>

rendering_engine::circle::circle(
    material* mat, float radius, unsigned int segments, float theta_start, float theta_length)
    : m_material{mat}, m_radius{radius}, m_segments{segments}, m_theta_start{theta_start}, m_theta_length{theta_length}
{
}

rendering_engine::circle::~circle()
{
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::circle::upload()
{
    // Build and upload through the asset cache, keyed by every geometry
    // parameter so two circles with identical parameters share one upload. The
    // builder only runs on a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "circle:" + cache_key_number(m_radius) + ":" + cache_key_number(m_segments) + ":" +
            cache_key_number(m_theta_start) + ":" + cache_key_number(m_theta_length) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int segments = m_segments < 3 ? 3 : m_segments;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(segments + 2);

            const core::math::vec3 normal{0.0f, 0.0f, 1.0f};

            // Centre vertex.
            assets::vertex_position_uv_normal centre;
            centre.pos = core::math::vec3{0.0f, 0.0f, 0.0f};
            centre.uv = core::math::vec2{0.5f, 0.5f};
            centre.normal = normal;
            vertices.push_back(centre);

            // Rim vertices, one extra so the last segment closes against a distinct
            // vertex.
            for (unsigned int s = 0; s <= segments; ++s)
            {
                const float segment =
                    m_theta_start + static_cast<float>(s) / static_cast<float>(segments) * m_theta_length;
                const float x = m_radius * std::cos(segment);
                const float y = m_radius * std::sin(segment);

                assets::vertex_position_uv_normal vertex;
                vertex.pos = core::math::vec3{x, y, 0.0f};
                vertex.uv = core::math::vec2{x / (2.0f * m_radius) + 0.5f, y / (2.0f * m_radius) + 0.5f};
                vertex.normal = normal;
                vertices.push_back(vertex);
            }

            std::vector<uint32_t> indices;
            indices.reserve(segments * 3);

            // Triangle fan. centre -> rim[s] -> rim[s + 1] traces a CCW loop when
            // viewed from +Z, so the +Z normal faces the camera on the front side.
            for (unsigned int s = 1; s <= segments; ++s)
            {
                indices.push_back(0);
                indices.push_back(s);
                indices.push_back(s + 1);
            }

            // Tangents complete the record for tangent-aware materials
            // (standard/PBR); the position/uv/normal offsets are unchanged so
            // materials that ignore the tangent still read correctly.
            const auto tangent_vertices = assets::generate_tangents(vertices, indices);
            return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
        });

    m_index_count = m_mesh->index_count;
    m_vertex_stride = m_mesh->vertex_stride;
}

bool rendering_engine::circle::world_bounds(core::math::aabb& out) const
{
    return mesh_world_bounds(m_mesh.get(), transform, out);
}

bool rendering_engine::circle::local_bounds(core::math::aabb& out) const
{
    return mesh_local_bounds(m_mesh.get(), transform, out);
}

void rendering_engine::circle::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("circle::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    if (!validate_vertex_format(*m_material, *m_mesh, "circle", m_vertex_format_reported))
    {
        return;
    }

    draw_item item{};
    item.mat = m_material;
    // The model + normal matrix the pass pushes (recomputed only when
    // the transform moved); a mirroring transform flags the item so the
    // pass draws it with the clockwise-front-face variant.
    m_per_draw.bind(transform, item);
    item.vertex_buffer = m_mesh->vertex_buffer;
    item.index_buffer = m_mesh->index_buffer;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}

rendering_engine::gpu::buffer rendering_engine::circle::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::circle::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::circle::get_index_count() const
{
    return m_index_count;
}
