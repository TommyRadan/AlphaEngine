// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/sphere.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/tangent.hpp>
#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/mesh_bounds.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <rendering_engine/renderables/vertex_format_check.hpp>
#include <runtime/engine.hpp>

rendering_engine::sphere::sphere(material* mat, unsigned int stacks, unsigned int slices)
    : m_material{mat}, m_stacks{stacks}, m_slices{slices}
{
}

rendering_engine::sphere::~sphere()
{
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::sphere::upload()
{
    // Build and upload through the asset cache, keyed by tessellation so two
    // spheres of the same resolution share one upload. The builder only runs on
    // a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "sphere:" + std::to_string(m_stacks) + "x" + std::to_string(m_slices) + ":" +
            vertex_format_name(vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int rings = m_stacks + 1;
            const unsigned int columns = m_slices + 1;

            std::vector<vertex_position_uv_normal> vertices;
            vertices.reserve(rings * columns);

            constexpr float pi = 3.14159265358979323846f;

            for (unsigned int i = 0; i < rings; ++i)
            {
                const float v = static_cast<float>(i) / static_cast<float>(m_stacks);
                const float phi = pi * v;
                const float sin_phi = std::sin(phi);
                const float cos_phi = std::cos(phi);

                for (unsigned int j = 0; j < columns; ++j)
                {
                    const float u = static_cast<float>(j) / static_cast<float>(m_slices);
                    const float theta = 2.0f * pi * u;
                    const float sin_theta = std::sin(theta);
                    const float cos_theta = std::cos(theta);

                    vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{sin_phi * cos_theta, sin_phi * sin_theta, cos_phi};
                    vertex.normal = vertex.pos;
                    vertex.uv = core::math::vec2{u, 1.0f - v};
                    vertices.push_back(vertex);
                }
            }

            std::vector<uint32_t> indices;
            indices.reserve(m_stacks * m_slices * 6);

            for (unsigned int i = 0; i < m_stacks; ++i)
            {
                for (unsigned int j = 0; j < m_slices; ++j)
                {
                    const uint32_t a = i * columns + j;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + columns;
                    const uint32_t d = c + 1;

                    // CCW winding when viewed from outside the sphere — going
                    // top-left -> bottom-left -> bottom-right traces a CCW loop
                    // in screen space when the outward normal points toward
                    // the camera.
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
            const auto tangent_vertices = generate_tangents(vertices, indices);
            return mesh_data::from_vertices(tangent_vertices, std::move(indices));
        });

    m_index_count = m_mesh->index_count;
    m_vertex_stride = m_mesh->vertex_stride;
}

bool rendering_engine::sphere::world_bounds(core::math::aabb& out) const
{
    return mesh_world_bounds(m_mesh.get(), transform, out);
}

bool rendering_engine::sphere::local_bounds(core::math::aabb& out) const
{
    return mesh_local_bounds(m_mesh.get(), transform, out);
}

void rendering_engine::sphere::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("sphere::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    if (!validate_vertex_format(*m_material, *m_mesh, "sphere", m_vertex_format_reported))
    {
        return;
    }

    draw_item item{};
    item.mat = m_material;
    // The model + normal matrix, pushed or put in this frame's per-draw
    // ring slot (recomputed only when the transform moved); a
    // mirroring transform flags the item so the pass draws it with
    // the clockwise-front-face variant.
    if (!m_per_draw.bind(transform, m_material->per_draw_layout(), item))
    {
        return;
    }
    item.vertex_buffer = m_mesh->vertex_buffer;
    item.index_buffer = m_mesh->index_buffer;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}

rendering_engine::gpu::buffer rendering_engine::sphere::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::sphere::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::sphere::get_index_count() const
{
    return m_index_count;
}
