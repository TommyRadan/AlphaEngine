// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/ui_draws.hpp>

#include <algorithm>
#include <initializer_list>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/ui_proxy.hpp>

namespace
{
    constexpr std::size_t vertices_per_quad = 4;
    constexpr std::size_t indices_per_quad = 6;

    // Smallest buffer a group or the index buffer is created with, in
    // quads; both then double as they grow.
    constexpr std::size_t min_quad_capacity = 16;

    std::size_t grown_capacity(std::size_t current, std::size_t needed)
    {
        std::size_t capacity = std::max(current, min_quad_capacity);
        while (capacity < needed)
        {
            capacity *= 2;
        }
        return capacity;
    }
} // namespace

namespace rendering_engine
{
    void ui_draw_builder::build(const render_world& world, gpu::device& device)
    {
        m_draws.clear();
        const std::span<const ui_proxy> proxies = world.ui_elements();
        for (std::size_t position = 0; position < proxies.size(); ++position)
        {
            const ui_proxy_handle handle = world.ui_element_at(position);
            const ui_proxy& proxy = proxies[position];

            if (handle.index >= m_resources.size())
            {
                m_resources.resize(static_cast<std::size_t>(handle.index) + 1);
            }
            proxy_resources& resources = m_resources[handle.index];
            if (resources.generation != handle.generation)
            {
                // The slot's previous proxy is gone; its resources go with it.
                release(device, resources);
                resources = proxy_resources{};
                resources.generation = handle.generation;
            }

            ui_material* mat = proxy.data.mat;
            if (mat == nullptr)
            {
                continue;
            }
            if (resources.revision != proxy.revision || resources.mat != mat)
            {
                sync(device, proxy, resources);
            }

            for (std::size_t index = 0; index < proxy.data.groups.size(); ++index)
            {
                const std::size_t quads = proxy.data.groups[index].vertices.size() / vertices_per_quad;
                const group_resources& group = resources.groups[index];
                draw_item item{};
                item.mat = mat;
                item.vertex_buffer = group.vertex_buffer;
                item.index_buffer = resources.index_buffer;
                item.per_draw_bind_group = group.bind_group;
                item.vertex_count = static_cast<uint32_t>(quads * vertices_per_quad);
                item.index_count = static_cast<uint32_t>(quads * indices_per_quad);
                item.vertex_stride = sizeof(ui_vertex);
                m_draws.push_back(item);
            }
        }

        // Proxies destroyed since the last frame leave resources behind in
        // slots no live proxy names; release them.
        for (std::size_t index = 0; index < m_resources.size(); ++index)
        {
            proxy_resources& resources = m_resources[index];
            if (resources.generation == 0)
            {
                continue;
            }
            const ui_proxy_handle handle{static_cast<uint32_t>(index), resources.generation};
            if (world.ui_element(handle) == nullptr)
            {
                release(device, resources);
                resources = proxy_resources{};
            }
        }
    }

    void ui_draw_builder::sync(gpu::device& device, const ui_proxy& proxy, proxy_resources& resources)
    {
        const std::vector<ui_quad_group>& groups = proxy.data.groups;
        ui_material& mat = *proxy.data.mat;
        if (resources.mat != &mat)
        {
            // Another material may lay its per-draw group out differently.
            for (group_resources& group : resources.groups)
            {
                if (group.bind_group.valid())
                {
                    device.destroy(group.bind_group);
                    group.bind_group = {};
                }
            }
            resources.mat = &mat;
        }

        // Groups beyond the proxy's count give their buffers back.
        while (resources.groups.size() > groups.size())
        {
            release(device, resources.groups.back());
            resources.groups.pop_back();
        }
        resources.groups.resize(groups.size());

        std::size_t largest = 0;
        for (const ui_quad_group& group : groups)
        {
            largest = std::max(largest, group.vertices.size() / vertices_per_quad);
        }
        reserve_indices(device, resources, largest);

        // Inside the frame bracket: the buffers are dynamic_data, so the
        // device writes this frame's copy of each (one per frame in flight)
        // and no frame still drawing reads it.
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            const ui_quad_group& source = groups[index];
            group_resources& group = resources.groups[index];
            const std::size_t quads = source.vertices.size() / vertices_per_quad;
            if (quads > group.capacity || !group.vertex_buffer.valid())
            {
                if (group.vertex_buffer.valid())
                {
                    device.destroy(group.vertex_buffer);
                }
                group.capacity = grown_capacity(group.capacity, quads);
                gpu::buffer_descriptor descriptor{};
                descriptor.size = group.capacity * vertices_per_quad * sizeof(ui_vertex);
                descriptor.usage = gpu::buffer_usage_vertex | gpu::buffer_usage_copy_dst;
                descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
                group.vertex_buffer = device.create_buffer(descriptor);
            }
            device.write_buffer(
                group.vertex_buffer, source.vertices.data(), source.vertices.size() * sizeof(ui_vertex), 0);

            if (group.bind_group.valid() && group.texture != source.texture)
            {
                device.destroy(group.bind_group);
                group.bind_group = {};
            }
            if (!group.bind_group.valid())
            {
                gpu::bind_group_descriptor descriptor{};
                descriptor.layout = mat.per_draw_layout();
                gpu::binding_value texture_slot{};
                texture_slot.binding = ui_material::texture_binding;
                texture_slot.kind = gpu::binding_kind::texture;
                texture_slot.texture_value = source.texture;
                descriptor.entries.push_back(texture_slot);
                group.bind_group = device.create_bind_group(descriptor);
                group.texture = source.texture;
            }
        }
        resources.revision = proxy.revision;
    }

    void ui_draw_builder::reserve_indices(gpu::device& device, proxy_resources& resources, std::size_t quads)
    {
        if (quads <= resources.index_capacity && resources.index_buffer.valid())
        {
            return;
        }
        if (resources.index_buffer.valid())
        {
            device.destroy(resources.index_buffer);
        }

        resources.index_capacity = grown_capacity(resources.index_capacity, quads);
        std::vector<uint32_t> indices;
        indices.reserve(resources.index_capacity * indices_per_quad);
        for (std::size_t quad = 0; quad < resources.index_capacity; ++quad)
        {
            const auto base = static_cast<uint32_t>(quad * vertices_per_quad);
            for (const uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                indices.push_back(base + corner);
            }
        }

        gpu::buffer_descriptor descriptor{};
        descriptor.size = indices.size() * sizeof(uint32_t);
        descriptor.usage = gpu::buffer_usage_index;
        descriptor.hint = gpu::buffer_usage_hint::static_data;
        descriptor.initial_data = indices.data();
        resources.index_buffer = device.create_buffer(descriptor);
    }

    void ui_draw_builder::release(gpu::device& device)
    {
        for (proxy_resources& resources : m_resources)
        {
            release(device, resources);
        }
        m_resources.clear();
        m_draws.clear();
    }

    void ui_draw_builder::release(gpu::device& device, group_resources& group)
    {
        if (group.bind_group.valid())
        {
            device.destroy(group.bind_group);
            group.bind_group = {};
        }
        if (group.vertex_buffer.valid())
        {
            device.destroy(group.vertex_buffer);
            group.vertex_buffer = {};
        }
        group.capacity = 0;
    }

    void ui_draw_builder::release(gpu::device& device, proxy_resources& resources)
    {
        for (group_resources& group : resources.groups)
        {
            release(device, group);
        }
        resources.groups.clear();
        if (resources.index_buffer.valid())
        {
            device.destroy(resources.index_buffer);
            resources.index_buffer = {};
        }
        resources.index_capacity = 0;
    }
} // namespace rendering_engine
