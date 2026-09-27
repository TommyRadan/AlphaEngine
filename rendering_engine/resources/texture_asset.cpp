// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/texture_asset.hpp>

#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    texture_asset::texture_asset(gpu::device& device) : m_device{&device} {}

    texture_asset::~texture_asset()
    {
        // The engine releases every asset handle (the cache, the renderer
        // and the scenes that hold them) before it destroys the device.
        // A placeholder handle belongs to the cache, not to this asset.
        if (owns_texture && texture.valid())
        {
            m_device->destroy(texture);
        }
    }
} // namespace rendering_engine
