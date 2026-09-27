// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/mesh_asset.hpp>

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
} // namespace rendering_engine
