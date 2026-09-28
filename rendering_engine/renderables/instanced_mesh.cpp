// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/instanced_mesh.hpp>

#include <cstdint>

#include <assets/mesh_data.hpp>
#include <core/log.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

rendering_engine::instanced_mesh::instanced_mesh(gpu::device& device, material* mat, uint32_t instance_count)
    : mesh_source{mat, "instanced_mesh"}, m_device{&device}, m_instance_count{instance_count},
      m_instances(instance_count)
{
}

void rendering_engine::instanced_mesh::upload_geometry(const std::vector<assets::vertex_position_uv_normal>& vertices,
                                                       const std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.empty())
    {
        LOG_WRN("instanced_mesh::upload_geometry: empty geometry");
        return;
    }

    const assets::mesh_data data = assets::mesh_data::from_vertices(vertices, indices);
    set_mesh(upload_mesh(*m_device, data, data.format));
    m_world_bounds_dirty = true;
}

void rendering_engine::instanced_mesh::set_geometry(std::shared_ptr<mesh_asset> mesh)
{
    set_mesh(std::move(mesh));
    m_world_bounds_dirty = true;
}

uint32_t rendering_engine::instanced_mesh::instance_capacity() const
{
    return static_cast<uint32_t>(m_instances.size());
}

void rendering_engine::instanced_mesh::reserve_instances(uint32_t capacity)
{
    if (capacity <= instance_capacity())
    {
        return;
    }
    // The new records keep mesh_instance's defaults (identity, white); the
    // next capture copies every record, since the slot count changed.
    m_instances.resize(capacity);
    changed();
}

void rendering_engine::instanced_mesh::mark_dirty(uint32_t index)
{
    if (m_dirty_begin == m_dirty_end)
    {
        m_dirty_begin = index;
        m_dirty_end = index + 1;
    }
    else
    {
        m_dirty_begin = index < m_dirty_begin ? index : m_dirty_begin;
        m_dirty_end = index + 1 > m_dirty_end ? index + 1 : m_dirty_end;
    }
    changed();
}

void rendering_engine::instanced_mesh::set_instance_count(uint32_t count)
{
    const uint32_t capacity = instance_capacity();
    const uint32_t clamped = count > capacity ? capacity : count;
    if (clamped != m_instance_count)
    {
        m_instance_count = clamped;
        m_world_bounds_dirty = true;
        changed();
    }
}

uint32_t rendering_engine::instanced_mesh::instance_count() const
{
    return m_instance_count;
}

void rendering_engine::instanced_mesh::set_instance_transform(uint32_t index, const core::math::mat4& transform)
{
    if (index >= instance_capacity())
    {
        LOG_WRN("instanced_mesh::set_instance_transform: index out of range");
        return;
    }
    m_instances[index].model = transform;
    m_world_bounds_dirty = true;
    mark_dirty(index);
}

void rendering_engine::instanced_mesh::set_instance_color(uint32_t index, const assets::color& color)
{
    if (index >= instance_capacity())
    {
        LOG_WRN("instanced_mesh::set_instance_color: index out of range");
        return;
    }
    m_instances[index].color = core::math::vec4{static_cast<float>(color.r) / 255.0f,
                                                static_cast<float>(color.g) / 255.0f,
                                                static_cast<float>(color.b) / 255.0f,
                                                static_cast<float>(color.a) / 255.0f};
    mark_dirty(index);
}

rendering_engine::mesh_description rendering_engine::instanced_mesh::describe() const
{
    mesh_description description = mesh_source::describe();
    description.placed = false;
    description.instanced = true;
    if (mesh() == nullptr || m_instance_count == 0)
    {
        description.bounds.reset();
        return description;
    }
    if (m_world_bounds_dirty)
    {
        // Union of the object-space box under every active instance
        // transform: each instance's transformed box is exact (the same
        // box the eight transformed corners span), so the union is the
        // tightest axis-aligned fit of the batch as a whole.
        const core::math::aabb& local = mesh()->bounds;
        m_world_bounds = core::math::transform(local, m_instances[0].model);
        for (uint32_t i = 1; i < m_instance_count; ++i)
        {
            m_world_bounds = core::math::merge(m_world_bounds, core::math::transform(local, m_instances[i].model));
        }
        m_world_bounds_dirty = false;
    }
    description.bounds = m_world_bounds;
    return description;
}

std::optional<core::math::aabb> rendering_engine::instanced_mesh::local_bounds() const
{
    return std::nullopt;
}

void rendering_engine::instanced_mesh::write_instances(render_world& world, mesh_proxy_handle proxy)
{
    mesh_indirect_args args{};
    args.index_count = mesh() != nullptr ? mesh()->index_count : 0;
    args.instance_count = m_instance_count;
    world.write_mesh_instances(proxy, m_instances, m_dirty_begin, m_dirty_end, args);
    m_dirty_begin = 0;
    m_dirty_end = 0;
}
