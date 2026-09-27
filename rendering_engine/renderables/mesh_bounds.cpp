// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/mesh_bounds.hpp>

#include <core/math/transform.hpp>
#include <rendering_engine/resources/mesh_asset.hpp>

namespace rendering_engine
{
    bool mesh_world_bounds(const mesh_asset* mesh, const core::transform& world, core::math::aabb& out)
    {
        if (mesh == nullptr)
        {
            return false;
        }
        out = core::math::transform(mesh->bounds, world.get_world_matrix());
        return true;
    }

    bool mesh_local_bounds(const mesh_asset* mesh, const core::transform& local, core::math::aabb& out)
    {
        if (mesh == nullptr)
        {
            return false;
        }
        out = core::math::transform(mesh->bounds, local.get_transform_matrix());
        return true;
    }
} // namespace rendering_engine
