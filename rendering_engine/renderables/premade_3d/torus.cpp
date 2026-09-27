// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/torus.hpp>

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

rendering_engine::torus::torus(
    material* mat, float radius, float tube, unsigned int radial_segments, unsigned int tubular_segments, float arc)
    : m_material{mat}, m_radius{radius}, m_tube{tube}, m_radial_segments{radial_segments},
      m_tubular_segments{tubular_segments}, m_arc{arc}
{
}

rendering_engine::torus::~torus()
{
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::torus::upload()
{
    // Build and upload through the asset cache, keyed by the torus geometry
    // parameters so two tori of the same shape share one upload. The builder
    // only runs on a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "torus:" + cache_key_number(m_radius) + ":" + cache_key_number(m_tube) + ":" +
            cache_key_number(m_radial_segments) + "x" + cache_key_number(m_tubular_segments) + ":" +
            cache_key_number(m_arc) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            // One extra ring/column of vertices so UVs and the seam wrap cleanly.
            const unsigned int rings = m_radial_segments + 1;
            const unsigned int columns = m_tubular_segments + 1;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(rings * columns);

            constexpr float pi = 3.14159265358979323846f;

            for (unsigned int j = 0; j < rings; ++j)
            {
                const float v = static_cast<float>(j) / static_cast<float>(m_radial_segments) * 2.0f * pi;
                const float sin_v = std::sin(v);
                const float cos_v = std::cos(v);

                for (unsigned int i = 0; i < columns; ++i)
                {
                    const float u = static_cast<float>(i) / static_cast<float>(m_tubular_segments) * m_arc;
                    const float sin_u = std::sin(u);
                    const float cos_u = std::cos(u);

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{
                        (m_radius + m_tube * cos_v) * cos_u, (m_radius + m_tube * cos_v) * sin_u, m_tube * sin_v};

                    // Normal points from the centre of the tube cross-section out to
                    // the surface point; for a torus this is the unit vector
                    // (cos_v*cos_u, cos_v*sin_u, sin_v).
                    const core::math::vec3 center{m_radius * cos_u, m_radius * sin_u, 0.0f};
                    vertex.normal = core::math::normalize(vertex.pos - center);

                    vertex.uv = core::math::vec2{static_cast<float>(i) / static_cast<float>(m_tubular_segments),
                                                 static_cast<float>(j) / static_cast<float>(m_radial_segments)};
                    vertices.push_back(vertex);
                }
            }

            std::vector<uint32_t> indices;
            indices.reserve(m_radial_segments * m_tubular_segments * 6);

            for (unsigned int j = 0; j < m_radial_segments; ++j)
            {
                for (unsigned int i = 0; i < m_tubular_segments; ++i)
                {
                    const uint32_t a = j * columns + i;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + columns;
                    const uint32_t d = c + 1;

                    // CCW winding when viewed from outside the torus, matching the
                    // sphere convention: a -> c -> d then a -> d -> b traces a CCW
                    // loop in screen space when the outward normal faces the camera.
                    indices.push_back(a);
                    indices.push_back(c);
                    indices.push_back(d);

                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(b);
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

bool rendering_engine::torus::world_bounds(core::math::aabb& out) const
{
    return mesh_world_bounds(m_mesh.get(), transform, out);
}

bool rendering_engine::torus::local_bounds(core::math::aabb& out) const
{
    return mesh_local_bounds(m_mesh.get(), transform, out);
}

void rendering_engine::torus::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("torus::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    if (!validate_vertex_format(*m_material, *m_mesh, "torus", m_vertex_format_reported))
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

rendering_engine::gpu::buffer rendering_engine::torus::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::torus::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::torus::get_index_count() const
{
    return m_index_count;
}
