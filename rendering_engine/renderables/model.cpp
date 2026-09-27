/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rendering_engine/renderables/model.hpp>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/mesh_bounds.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/vertex_format_check.hpp>
#include <runtime/engine.hpp>

rendering_engine::model::model(material* mat) : m_material{mat} {}

rendering_engine::model::~model()
{
    auto& gpu = *runtime::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
    if (m_joint_buffer.valid())
    {
        gpu.destroy(m_joint_buffer);
        m_joint_buffer = {};
    }
    if (m_draw_ubo.valid())
    {
        gpu.destroy(m_draw_ubo);
        m_draw_ubo = {};
    }
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
}

void rendering_engine::model::upload_mesh(const rendering_engine::mesh& mesh)
{
    m_vertex_count = mesh.vertex_count();
    m_vertex_stride = sizeof(vertex_position_uv_normal);
    m_vertex_format = vertex_format::position_uv_normal;
    m_has_local_bounds = false;

    if (m_vertex_count == 0)
    {
        LOG_WRN("model::upload_mesh: mesh has no vertices; nothing uploaded");
        return;
    }

    // Box the vertices once at upload so world_bounds is a matrix
    // transform per frame, not a pass over the vertex array.
    if (const auto bounds = compute_position_bounds(
            mesh.vertices(), m_vertex_count * sizeof(vertex_position_uv_normal), m_vertex_stride);
        bounds.has_value())
    {
        m_local_bounds = *bounds;
        m_has_local_bounds = true;
    }

    auto& gpu = *runtime::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = m_vertex_count * sizeof(vertex_position_uv_normal);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = mesh.vertices();
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);
}

void rendering_engine::model::set_mesh(std::shared_ptr<mesh_asset> mesh)
{
    m_mesh = std::move(mesh);
    m_vertex_stride = m_mesh ? m_mesh->vertex_stride : 0;
    m_vertex_format = m_mesh ? m_mesh->format : vertex_format::custom;
    m_vertex_format_reported = false;
}

void rendering_engine::model::set_joint_matrices(std::span<const core::math::mat4> matrices)
{
    m_joint_matrices.assign(matrices.begin(), matrices.end());
    m_joints_dirty = true;
}

size_t rendering_engine::model::joint_count() const
{
    return m_joint_matrices.size();
}

bool rendering_engine::model::casts_shadow() const
{
    return m_material == nullptr || !m_material->is_skinned();
}

bool rendering_engine::model::world_bounds(core::math::aabb& out) const
{
    if (m_material != nullptr && m_material->is_skinned())
    {
        return false;
    }
    if (m_mesh)
    {
        return mesh_world_bounds(m_mesh.get(), transform, out);
    }
    if (!m_has_local_bounds)
    {
        return false;
    }
    out = core::math::transform(m_local_bounds, transform.get_world_matrix());
    return true;
}

void rendering_engine::model::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("model::collect_draw_items: no material");
        return;
    }

    // Draw the shared cached mesh if one was set, otherwise the buffer uploaded
    // privately via upload_mesh.
    const gpu::buffer vertex_buffer = m_mesh ? m_mesh->vertex_buffer : m_vertex_buffer;
    const uint32_t vertex_count = m_mesh ? m_mesh->vertex_count : static_cast<uint32_t>(m_vertex_count);
    if (!vertex_buffer.valid())
    {
        return;
    }
    if (!validate_vertex_format(*m_material, m_vertex_format, m_vertex_stride, "model", m_vertex_format_reported))
    {
        return;
    }

    const bool skinned = m_material->is_skinned();
    if (skinned && m_joint_matrices.empty())
    {
        // The vertex stage would index an empty palette; wait for the
        // animation system to supply one.
        if (!m_missing_joints_reported)
        {
            m_missing_joints_reported = true;
            LOG_WRN("model::collect_draw_items: skinned material but no joint matrices set; skipping draw");
        }
        return;
    }

    auto& gpu = *runtime::current_engine().gpu;

    if (!m_draw_ubo.valid())
    {
        // The PerDraw block: model + normal matrix (see per_draw_ubo.hpp).
        m_draw_ubo = create_per_draw_ubo(gpu);
    }

    const gpu::bind_group_layout layout = m_material->per_draw_layout();
    if (m_draw_bind_group.valid() && m_draw_bind_group_layout != layout)
    {
        // The material moved between its rigid and skinned variants.
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }

    if (skinned)
    {
        if (!m_joint_buffer.valid() || m_joint_capacity < m_joint_matrices.size())
        {
            // A larger palette than the buffer holds: reallocate, and
            // rebuild the group that references the old buffer.
            if (m_joint_buffer.valid())
            {
                gpu.destroy(m_joint_buffer);
            }
            if (m_draw_bind_group.valid())
            {
                gpu.destroy(m_draw_bind_group);
                m_draw_bind_group = {};
            }
            m_joint_buffer = create_joint_buffer(gpu, m_joint_matrices.size());
            m_joint_capacity = m_joint_matrices.size();
            m_joints_dirty = true;
        }
        if (m_joints_dirty)
        {
            write_joint_buffer(gpu, m_joint_buffer, m_joint_matrices);
            m_joints_dirty = false;
        }
    }

    if (!m_draw_bind_group.valid())
    {
        m_draw_bind_group = skinned ? create_skinned_per_draw_bind_group(gpu, layout, m_draw_ubo, m_joint_buffer)
                                    : create_per_draw_bind_group(gpu, layout, m_draw_ubo);
        m_draw_bind_group_layout = layout;
    }

    // Upload the model + normal matrix; a mirroring transform flags the
    // item so the pass draws it with the clockwise-front-face variant.
    const bool mirrored = write_per_draw_ubo(gpu, m_draw_ubo, transform.get_world_matrix());

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = vertex_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.mirrored = mirrored;
    item.vertex_count = vertex_count;
    item.vertex_stride = m_vertex_stride;
    // A cached asset that carries indices is drawn indexed; the private
    // upload_mesh path is always a plain vertex array.
    if (m_mesh && m_mesh->index_buffer.valid())
    {
        item.index_buffer = m_mesh->index_buffer;
        item.index_count = m_mesh->index_count;
        item.index_format = gpu::index_format::uint32;
    }
    out.push_back(item);
}
