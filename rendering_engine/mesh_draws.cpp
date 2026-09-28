// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/mesh_draws.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_world.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

namespace
{
    // The per-instance stream must match the stride the instanced
    // material's slot-1 vertex layout was built with.
    static_assert(sizeof(rendering_engine::mesh_instance) ==
                      rendering_engine::instanced_material::instance_buffer_stride,
                  "mesh_instance must match instanced_material::instance_buffer_stride");
} // namespace

namespace rendering_engine
{
    void mesh_draw_builder::build(render_world& world, gpu::device& device)
    {
        const std::span<const mesh_proxy> proxies = world.meshes();
        m_draws.clear();
        m_draws.reserve(proxies.size());

        for (std::size_t position = 0; position < proxies.size(); ++position)
        {
            const mesh_proxy_handle handle = world.mesh_at(position);
            // Mutable: uploading the instance snapshot consumes its pending
            // range.
            mesh_proxy& proxy = *world.mesh(handle);

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

            const mesh_description& source = proxy.source;
            const bool skinned = source.mat != nullptr && source.mat->is_skinned();
            const mesh_asset* geometry = source.mesh.get();

            mesh_draw& draw = m_draws.emplace_back();
            draw.layer_mask = source.layer_mask;
            draw.bounds = proxy.world_bounds;
            draw.bounded = source.bounds.has_value() && !skinned;
            draw.casts_shadow = source.casts_shadow && !skinned;

            bool drawable = proxy.visible && source.mat != nullptr && geometry != nullptr &&
                            geometry->vertex_buffer.valid() && proxy.format_ok;
            if (!drawable)
            {
                continue;
            }

            draw_item& item = draw.item;
            item.mat = source.mat;
            item.vertex_buffer = geometry->vertex_buffer;
            item.vertex_stride = geometry->vertex_stride;
            item.vertex_count = source.vertex_count.value_or(geometry->vertex_count);
            // Geometry that carries indices is drawn indexed.
            if (geometry->index_buffer.valid())
            {
                item.index_buffer = geometry->index_buffer;
                item.index_count = geometry->index_count;
                item.index_format = gpu::index_format::uint32;
            }

            if (source.instanced)
            {
                // One indexed-indirect draw over the shared geometry: the
                // per-instance stream carries the transforms (bound to
                // vertex slot 1), the indirect record the counts.
                drawable = geometry->index_buffer.valid() && sync_instances(device, proxy, resources);
                if (drawable)
                {
                    item.indirect_buffer = resources.indirect_buffer;
                    item.instance_buffer = resources.instance_buffer;
                    item.instance_stride = static_cast<uint32_t>(sizeof(mesh_instance));
                    item.instance_count = proxy.instances.args.instance_count;
                }
            }
            else
            {
                // The model + normal matrix the pass pushes; a mirroring
                // placement draws with the clockwise-front-face variant. A
                // skinned draw also binds its joint palette.
                item.per_draw_push = &proxy.per_draw;
                item.mirrored = proxy.mirrored;
                if (skinned)
                {
                    drawable = sync_joints(device, proxy, resources);
                    item.per_draw_bind_group = resources.skin_group;
                }
            }
            draw.drawable = drawable;
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
            const mesh_proxy_handle handle{static_cast<uint32_t>(index), resources.generation};
            if (world.mesh(handle) == nullptr)
            {
                release(device, resources);
                resources = proxy_resources{};
            }
        }
    }

    bool mesh_draw_builder::sync_instances(gpu::device& device, mesh_proxy& proxy, proxy_resources& resources)
    {
        mesh_instances& snapshot = proxy.instances;
        const auto capacity = static_cast<uint32_t>(snapshot.records.size());
        if (snapshot.args.instance_count == 0 || capacity == 0)
        {
            return false;
        }

        // The per-instance stream, reallocated when the snapshot outgrew it;
        // the old one is released through the device, which defers the free
        // until no frame still reads it. A fresh stream holds nothing, so
        // every record is uploaded, and a later count increase never samples
        // uninitialised storage.
        if (!resources.instance_buffer.valid() || resources.instance_capacity < capacity)
        {
            if (resources.instance_buffer.valid())
            {
                device.destroy(resources.instance_buffer);
            }
            gpu::buffer_descriptor instance_descriptor{};
            instance_descriptor.size = static_cast<size_t>(capacity) * sizeof(mesh_instance);
            instance_descriptor.usage = gpu::buffer_usage_vertex | gpu::buffer_usage_copy_dst;
            instance_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            resources.instance_buffer = device.create_buffer(instance_descriptor);
            resources.instance_capacity = resources.instance_buffer.valid() ? capacity : 0;
            snapshot.dirty_begin = 0;
            snapshot.dirty_end = capacity;
            if (!resources.instance_buffer.valid())
            {
                return false;
            }
        }

        // The indirect record: its index-count field selects the geometry,
        // its instance-count field how many copies the one draw paints.
        if (!resources.indirect_buffer.valid())
        {
            gpu::buffer_descriptor indirect_descriptor{};
            indirect_descriptor.size = sizeof(mesh_indirect_args);
            indirect_descriptor.usage = gpu::buffer_usage_indirect | gpu::buffer_usage_copy_dst;
            indirect_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            resources.indirect_buffer = device.create_buffer(indirect_descriptor);
            resources.indirect_written = false;
        }

        // Only the records captured since the last upload.
        if (snapshot.dirty_end > snapshot.dirty_begin)
        {
            device.write_buffer(resources.instance_buffer,
                                snapshot.records.data() + snapshot.dirty_begin,
                                static_cast<size_t>(snapshot.dirty_end - snapshot.dirty_begin) * sizeof(mesh_instance),
                                static_cast<size_t>(snapshot.dirty_begin) * sizeof(mesh_instance));
            snapshot.dirty_begin = 0;
            snapshot.dirty_end = 0;
        }

        // Rewritten whenever an argument changed: the instance count, or the
        // index count after a geometry swap (a stale one would draw the old
        // mesh's element range over the new buffers).
        if (!resources.indirect_written || resources.indirect_args != snapshot.args)
        {
            device.write_buffer(resources.indirect_buffer, &snapshot.args, sizeof(mesh_indirect_args), 0);
            resources.indirect_args = snapshot.args;
            resources.indirect_written = true;
        }
        return true;
    }

    bool mesh_draw_builder::sync_joints(gpu::device& device, const mesh_proxy& proxy, proxy_resources& resources)
    {
        if (proxy.joints.empty())
        {
            // The vertex stage would index an empty palette; wait for the
            // animation to supply one.
            if (!resources.missing_joints_reported)
            {
                resources.missing_joints_reported = true;
                LOG_WRN("%s: skinned material but no joint matrices set; skipping draw", proxy.source.name);
            }
            return false;
        }

        const gpu::bind_group_layout layout = proxy.source.mat->per_draw_layout();
        if (resources.skin_group.valid() && resources.skin_layout != layout)
        {
            // The material moved to another skinned variant layout.
            device.destroy(resources.skin_group);
            resources.skin_group = {};
        }

        if (!resources.joint_buffer.valid() || resources.joint_capacity < proxy.joints.size())
        {
            // A larger palette than the buffer holds: reallocate, and rebuild
            // the group that references the old buffer.
            if (resources.joint_buffer.valid())
            {
                device.destroy(resources.joint_buffer);
            }
            if (resources.skin_group.valid())
            {
                device.destroy(resources.skin_group);
                resources.skin_group = {};
            }
            resources.joint_buffer = create_joint_buffer(device, proxy.joints.size());
            resources.joint_capacity = proxy.joints.size();
            resources.joints_revision = 0;
        }
        if (resources.joints_revision != proxy.joints_revision)
        {
            write_joint_buffer(device, resources.joint_buffer, proxy.joints);
            resources.joints_revision = proxy.joints_revision;
        }

        if (!resources.skin_group.valid())
        {
            resources.skin_group = create_skinned_per_draw_bind_group(device, layout, resources.joint_buffer);
            resources.skin_layout = layout;
        }
        return resources.skin_group.valid();
    }

    void mesh_draw_builder::release(gpu::device& device)
    {
        for (proxy_resources& resources : m_resources)
        {
            release(device, resources);
        }
        m_resources.clear();
        m_draws.clear();
    }

    void mesh_draw_builder::release(gpu::device& device, proxy_resources& resources)
    {
        if (resources.skin_group.valid())
        {
            device.destroy(resources.skin_group);
            resources.skin_group = {};
        }
        if (resources.joint_buffer.valid())
        {
            device.destroy(resources.joint_buffer);
            resources.joint_buffer = {};
        }
        if (resources.indirect_buffer.valid())
        {
            device.destroy(resources.indirect_buffer);
            resources.indirect_buffer = {};
        }
        if (resources.instance_buffer.valid())
        {
            device.destroy(resources.instance_buffer);
            resources.instance_buffer = {};
        }
    }
} // namespace rendering_engine
