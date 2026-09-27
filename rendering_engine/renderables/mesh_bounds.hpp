// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/aabb.hpp>

namespace core
{
    struct transform;
}

namespace rendering_engine
{
    struct mesh_asset;

    // World-space box of a cached @p mesh drawn under @p world: the asset's
    // object-space @c bounds transformed by the world matrix and re-boxed.
    // Shared by every renderable that draws a @ref mesh_asset (the
    // @c premade_3d primitives, @ref model) to answer
    // @ref renderable::world_bounds. Returns false and leaves @p out
    // untouched when @p mesh is null (nothing uploaded yet), so the caller
    // is treated as unbounded rather than culled.
    bool mesh_world_bounds(const mesh_asset* mesh, const core::transform& world, core::math::aabb& out);

    // The same box in the space of @p local's parent: the asset's bounds
    // under @p local's own matrix only, for @ref renderable::local_bounds.
    // Returns false and leaves @p out untouched when @p mesh is null.
    bool mesh_local_bounds(const mesh_asset* mesh, const core::transform& local, core::math::aabb& out);
} // namespace rendering_engine
