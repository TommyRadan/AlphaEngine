// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file drawn_bounds.hpp
 * @brief The CPU-side extent of what a node draws, for world systems that
 *        size themselves to it without depending on the renderer.
 */

#pragma once

#include <optional>

#include <core/math/aabb.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief The box, in @p owner's local space, around the geometry the
     *        node draws, or @c std::nullopt when it draws nothing boxed.
     *
     * The object-space bounds of its @ref mesh_component's mesh, kept on the
     * CPU with the mesh asset (or computed from the mesh data a component
     * built from a private upload was given); else the box of the premade
     * shape its @ref renderable_component holds, which is that shape's mesh
     * asset bounds under the shape's own local transform. Physics fits a
     * collider to it (see @c runtime::physics::collider_settings).
     */
    std::optional<core::math::aabb> drawn_bounds(node& owner);
} // namespace runtime
