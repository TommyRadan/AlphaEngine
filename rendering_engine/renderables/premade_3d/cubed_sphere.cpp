// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/cubed_sphere.hpp>

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
#include <runtime/engine.hpp>

namespace
{
    // Per-face anchors. For each cube face, `origin` is the
    // (u=0, v=0) corner of the face on the unit cube and
    // (`u_axis`, `v_axis`) are the world-space vectors that traverse
    // the face from u=0->1 and v=0->1 respectively. Conventions match
    // standard (Vulkan) cube maps: u increases to the right when looking
    // at the face from outside, v increases downward.
    struct face_def
    {
        core::math::vec3 origin;
        core::math::vec3 u_axis;
        core::math::vec3 v_axis;
    };

    const face_def faces[6] = {
        // +X (right)
        {core::math::vec3{+1.0f, +1.0f, +1.0f},
         core::math::vec3{0.0f, 0.0f, -2.0f},
         core::math::vec3{0.0f, -2.0f, 0.0f}},
        // -X (left)
        {core::math::vec3{-1.0f, +1.0f, -1.0f},
         core::math::vec3{0.0f, 0.0f, +2.0f},
         core::math::vec3{0.0f, -2.0f, 0.0f}},
        // +Y (top)
        {core::math::vec3{-1.0f, +1.0f, -1.0f},
         core::math::vec3{+2.0f, 0.0f, 0.0f},
         core::math::vec3{0.0f, 0.0f, +2.0f}},
        // -Y (bottom)
        {core::math::vec3{-1.0f, -1.0f, +1.0f},
         core::math::vec3{+2.0f, 0.0f, 0.0f},
         core::math::vec3{0.0f, 0.0f, -2.0f}},
        // +Z (back)
        {core::math::vec3{-1.0f, +1.0f, +1.0f},
         core::math::vec3{+2.0f, 0.0f, 0.0f},
         core::math::vec3{0.0f, -2.0f, 0.0f}},
        // -Z (front)
        {core::math::vec3{+1.0f, +1.0f, -1.0f},
         core::math::vec3{-2.0f, 0.0f, 0.0f},
         core::math::vec3{0.0f, -2.0f, 0.0f}},
    };
} // namespace

rendering_engine::cubed_sphere::cubed_sphere(material* mat, unsigned int subdivisions)
    : m_material{mat}, m_subdivisions{subdivisions}
{
}

rendering_engine::cubed_sphere::~cubed_sphere()
{
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::cubed_sphere::upload()
{
    // Build and upload through the asset cache, keyed by the per-face
    // subdivision so two cubed spheres of the same resolution share one
    // upload. The builder only runs on a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "cubed_sphere:" + std::to_string(m_subdivisions) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int n = m_subdivisions;
            const unsigned int rows = n + 1;
            const unsigned int verts_per_face = rows * rows;
            const unsigned int quads_per_face = n * n;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(6 * verts_per_face);

            std::vector<uint32_t> indices;
            indices.reserve(6 * quads_per_face * 6);

            for (int face = 0; face < 6; ++face)
            {
                const auto& f = faces[face];
                const uint32_t base = static_cast<uint32_t>(vertices.size());

                for (unsigned int j = 0; j < rows; ++j)
                {
                    const float v = static_cast<float>(j) / static_cast<float>(n);
                    for (unsigned int i = 0; i < rows; ++i)
                    {
                        const float u = static_cast<float>(i) / static_cast<float>(n);
                        const core::math::vec3 cube_pos = f.origin + u * f.u_axis + v * f.v_axis;
                        const core::math::vec3 sphere_pos = core::math::normalize(cube_pos);

                        assets::vertex_position_uv_normal vertex;
                        vertex.pos = sphere_pos;
                        vertex.normal = sphere_pos;
                        vertex.uv = core::math::vec2{u, v};
                        vertices.push_back(vertex);
                    }
                }

                for (unsigned int j = 0; j < n; ++j)
                {
                    for (unsigned int i = 0; i < n; ++i)
                    {
                        const uint32_t a = base + j * rows + i; // (i,   j)   — top-left
                        const uint32_t b = a + 1;               // (i+1, j)   — top-right
                        const uint32_t c = a + rows;            // (i,   j+1) — bottom-left
                        const uint32_t d = c + 1;               // (i+1, j+1) — bottom-right

                        // CCW winding when viewed from outside: TL -> BL -> BR
                        indices.push_back(a);
                        indices.push_back(c);
                        indices.push_back(d);

                        indices.push_back(a);
                        indices.push_back(d);
                        indices.push_back(b);
                    }
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

bool rendering_engine::cubed_sphere::world_bounds(core::math::aabb& out) const
{
    return mesh_world_bounds(m_mesh.get(), transform, out);
}

bool rendering_engine::cubed_sphere::local_bounds(core::math::aabb& out) const
{
    return mesh_local_bounds(m_mesh.get(), transform, out);
}

void rendering_engine::cubed_sphere::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("cubed_sphere::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    if (!validate_vertex_format(*m_material, *m_mesh, "cubed_sphere", m_vertex_format_reported))
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

rendering_engine::gpu::buffer rendering_engine::cubed_sphere::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::cubed_sphere::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::cubed_sphere::get_index_count() const
{
    return m_index_count;
}
