// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/plane.hpp>

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

rendering_engine::plane::plane(
    material* mat, float width, float height, unsigned int width_segments, unsigned int height_segments)
    : m_material{mat}, m_width{width}, m_height{height}, m_width_segments{width_segments},
      m_height_segments{height_segments}
{
}

rendering_engine::plane::~plane()
{
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::plane::upload()
{
    // Build and upload through the asset cache, keyed by dimensions and segment
    // counts so two planes of the same geometry share one upload. The builder
    // only runs on a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "plane:" + cache_key_number(m_width) + "x" + cache_key_number(m_height) + ":" +
            cache_key_number(m_width_segments) + "x" + cache_key_number(m_height_segments) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int columns = m_width_segments + 1;
            const unsigned int rows = m_height_segments + 1;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(columns * rows);

            const float half_width = m_width * 0.5f;
            const float half_height = m_height * 0.5f;

            for (unsigned int i = 0; i < rows; ++i)
            {
                const float v = static_cast<float>(i) / static_cast<float>(m_height_segments);
                const float y = v * m_height - half_height;

                for (unsigned int j = 0; j < columns; ++j)
                {
                    const float u = static_cast<float>(j) / static_cast<float>(m_width_segments);
                    const float x = u * m_width - half_width;

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{x, y, 0.0f};
                    vertex.uv = core::math::vec2{u, 1.0f - v};
                    vertex.normal = core::math::vec3{0.0f, 0.0f, 1.0f};
                    vertices.push_back(vertex);
                }
            }

            std::vector<uint32_t> indices;
            indices.reserve(m_width_segments * m_height_segments * 6);

            for (unsigned int i = 0; i < m_height_segments; ++i)
            {
                for (unsigned int j = 0; j < m_width_segments; ++j)
                {
                    const uint32_t a = i * columns + j;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + columns;
                    const uint32_t d = c + 1;

                    // CCW winding when viewed from the +Z side (the front face),
                    // so the +Z normal points toward the camera looking down -Z.
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);

                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(c);
                }
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

bool rendering_engine::plane::world_bounds(core::math::aabb& out) const
{
    return mesh_world_bounds(m_mesh.get(), transform, out);
}

bool rendering_engine::plane::local_bounds(core::math::aabb& out) const
{
    return mesh_local_bounds(m_mesh.get(), transform, out);
}

void rendering_engine::plane::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("plane::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    if (!validate_vertex_format(*m_material, *m_mesh, "plane", m_vertex_format_reported))
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

rendering_engine::gpu::buffer rendering_engine::plane::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::plane::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::plane::get_index_count() const
{
    return m_index_count;
}
