// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/mesh_asset.hpp>

#include <utility>

#include <assets/mesh_data.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    mesh_asset::mesh_asset(gpu::device& device) : m_device{&device} {}

    mesh_asset::~mesh_asset()
    {
        // Free in the reverse of the create order used by the cache, matching
        // the premade renderables' teardown. The engine releases every asset
        // handle before it destroys the device.
        if (index_buffer.valid())
        {
            m_device->destroy(index_buffer);
        }
        if (vertex_buffer.valid())
        {
            m_device->destroy(vertex_buffer);
        }
    }

    std::shared_ptr<mesh_asset>
    upload_mesh(gpu::device& device, const assets::mesh_data& data, assets::vertex_format format, std::string key)
    {
        if (data.vertex_stride == 0 || data.vertex_bytes.size() < data.vertex_stride)
        {
            return nullptr;
        }

        auto asset = std::make_shared<mesh_asset>(device);
        asset->key = std::move(key);

        gpu::buffer_descriptor vertex_descriptor{};
        vertex_descriptor.size = data.vertex_bytes.size();
        vertex_descriptor.usage = gpu::buffer_usage_vertex;
        vertex_descriptor.initial_data = data.vertex_bytes.data();
        asset->vertex_buffer = device.create_buffer(vertex_descriptor);
        asset->vertex_stride = data.vertex_stride;
        asset->format = format;
        asset->vertex_count = static_cast<uint32_t>(data.vertex_bytes.size() / data.vertex_stride);

        // Object-space bounds for frustum culling: trust the builder's box
        // when it supplied one (an importer's record may not lead with the
        // position), otherwise derive it from the positions once here so
        // every renderable sharing this upload shares the box too.
        if (data.bounds.has_value())
        {
            asset->bounds = *data.bounds;
        }
        else if (const auto computed = data.compute_bounds(); computed.has_value())
        {
            asset->bounds = *computed;
        }

        if (!data.indices.empty())
        {
            gpu::buffer_descriptor index_descriptor{};
            index_descriptor.size = data.indices.size() * sizeof(uint32_t);
            index_descriptor.usage = gpu::buffer_usage_index;
            index_descriptor.initial_data = data.indices.data();
            asset->index_buffer = device.create_buffer(index_descriptor);
            asset->index_count = static_cast<uint32_t>(data.indices.size());
        }
        return asset;
    }
} // namespace rendering_engine
