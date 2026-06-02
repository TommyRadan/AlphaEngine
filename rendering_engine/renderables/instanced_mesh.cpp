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

#include <rendering_engine/renderables/instanced_mesh.hpp>

#include <array>
#include <cstdint>

#include <core/log.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/materials/material.hpp>
#include <runtime/engine.hpp>

namespace
{
    // A DrawElementsIndirectCommand record: index count, instance count,
    // first index, base vertex, base instance. Mirrors GL's
    // @c glDrawElementsIndirect struct and Vulkan's
    // @c VkDrawIndexedIndirectCommand, the five-uint32 shape the device's
    // @c draw_indexed_indirect documents.
    constexpr size_t indirect_command_uints = 5;
    constexpr size_t indirect_command_size = indirect_command_uints * sizeof(uint32_t);
} // namespace

rendering_engine::instanced_mesh::instanced_mesh(material* mat, uint32_t instance_count)
    : m_material{mat}, m_capacity{instance_count}, m_instance_count{instance_count}, m_instances(instance_count)
{
}

rendering_engine::instanced_mesh::~instanced_mesh()
{
    auto& gpu = *runtime::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
    if (m_indirect_buffer.valid())
    {
        gpu.destroy(m_indirect_buffer);
        m_indirect_buffer = {};
    }
    if (m_instance_buffer.valid())
    {
        gpu.destroy(m_instance_buffer);
        m_instance_buffer = {};
    }
    if (m_index_buffer.valid())
    {
        gpu.destroy(m_index_buffer);
        m_index_buffer = {};
    }
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
}

void rendering_engine::instanced_mesh::upload_geometry(const std::vector<vertex_position_uv_normal>& vertices,
                                                       const std::vector<uint32_t>& indices)
{
    if (vertices.empty() || indices.empty())
    {
        LOG_WRN("instanced_mesh::upload_geometry: empty geometry");
        return;
    }

    m_index_count = static_cast<uint32_t>(indices.size());
    m_vertex_stride = sizeof(vertex_position_uv_normal);

    auto& gpu = *runtime::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = vertices.size() * sizeof(vertex_position_uv_normal);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = vertices.data();
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

    gpu::buffer_descriptor index_descriptor{};
    index_descriptor.size = indices.size() * sizeof(uint32_t);
    index_descriptor.usage = gpu::buffer_usage_index;
    index_descriptor.hint = gpu::buffer_usage_hint::static_data;
    index_descriptor.initial_data = indices.data();
    m_index_buffer = gpu.create_buffer(index_descriptor);
}

uint32_t rendering_engine::instanced_mesh::instance_capacity() const
{
    return m_capacity;
}

void rendering_engine::instanced_mesh::set_instance_count(uint32_t count)
{
    m_instance_count = count > m_capacity ? m_capacity : count;
}

uint32_t rendering_engine::instanced_mesh::instance_count() const
{
    return m_instance_count;
}

void rendering_engine::instanced_mesh::set_instance_transform(uint32_t index, const core::math::mat4& transform)
{
    if (index >= m_capacity)
    {
        LOG_WRN("instanced_mesh::set_instance_transform: index out of range");
        return;
    }
    m_instances[index].model = transform;
    m_instances_dirty = true;
}

void rendering_engine::instanced_mesh::set_instance_color(uint32_t index, const util::color& color)
{
    if (index >= m_capacity)
    {
        LOG_WRN("instanced_mesh::set_instance_color: index out of range");
        return;
    }
    m_instances[index].color = core::math::vec4{static_cast<float>(color.r) / 255.0f,
                                                static_cast<float>(color.g) / 255.0f,
                                                static_cast<float>(color.b) / 255.0f,
                                                static_cast<float>(color.a) / 255.0f};
    m_instances_dirty = true;
}

void rendering_engine::instanced_mesh::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("instanced_mesh::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
    {
        return;
    }
    if (m_instance_count == 0 || m_capacity == 0)
    {
        return;
    }

    auto& gpu = *runtime::current_engine().gpu;

    // Per-instance storage buffer: one {mat4 model; vec4 color;} record per
    // instance, indexed by gl_InstanceIndex in the instanced material.
    if (!m_instance_buffer.valid())
    {
        gpu::buffer_descriptor instance_descriptor{};
        instance_descriptor.size = m_capacity * sizeof(instance_record);
        instance_descriptor.usage = gpu::buffer_usage_storage | gpu::buffer_usage_copy_dst;
        instance_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_instance_buffer = gpu.create_buffer(instance_descriptor);
        m_instances_dirty = true;
    }

    // Indirect command buffer holding a single DrawElementsIndirectCommand;
    // its instance-count field drives how many copies the one draw paints.
    if (!m_indirect_buffer.valid())
    {
        gpu::buffer_descriptor indirect_descriptor{};
        indirect_descriptor.size = indirect_command_size;
        indirect_descriptor.usage = gpu::buffer_usage_indirect | gpu::buffer_usage_copy_dst;
        indirect_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_indirect_buffer = gpu.create_buffer(indirect_descriptor);

        const std::array<uint32_t, indirect_command_uints> command{m_index_count, m_instance_count, 0u, 0u, 0u};
        gpu.write_buffer(m_indirect_buffer, command.data(), indirect_command_size, 0);
        m_uploaded_instance_count = m_instance_count;
    }

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_material->per_draw_layout();
        gpu::binding_value instance_slot{};
        instance_slot.binding = instanced_material::instance_buffer_binding;
        instance_slot.kind = gpu::binding_kind::storage_buffer;
        instance_slot.buffer_value = m_instance_buffer;
        bg_descriptor.entries.push_back(instance_slot);
        m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
    }

    // Re-upload the whole per-instance array when any record changed. The
    // full capacity is written so a later count increase never samples
    // uninitialised storage.
    if (m_instances_dirty)
    {
        gpu.write_buffer(m_instance_buffer, m_instances.data(), m_capacity * sizeof(instance_record), 0);
        m_instances_dirty = false;
    }

    // Refresh only the instance-count field of the command when the active
    // count changed (the index count is fixed by the geometry).
    if (m_uploaded_instance_count != m_instance_count)
    {
        const std::array<uint32_t, indirect_command_uints> command{m_index_count, m_instance_count, 0u, 0u, 0u};
        gpu.write_buffer(m_indirect_buffer, command.data(), indirect_command_size, 0);
        m_uploaded_instance_count = m_instance_count;
    }

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.index_buffer = m_index_buffer;
    item.indirect_buffer = m_indirect_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}
