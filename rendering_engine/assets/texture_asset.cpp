// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/assets/texture_asset.hpp>

#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    texture_asset::~texture_asset()
    {
        // Resolve the live device through the asset-layer accessor and release
        // the handle. The engine tears the asset cache (and the renderer /
        // scenes that hold these handles) down ahead of the gpu device, so the
        // device is always installed when this runs.
        // A placeholder handle belongs to the cache, not to this asset.
        if (owns_texture && texture.valid())
        {
            asset_device().destroy(texture);
        }
    }
} // namespace rendering_engine
