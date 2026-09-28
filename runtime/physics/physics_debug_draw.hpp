// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file physics_debug_draw.hpp
 * @brief Draws the physics world's colliders and contacts through the
 *        debug-draw functions.
 */

#pragma once

namespace rendering_engine
{
    struct render_world;
}

namespace runtime::physics
{
    struct world;

    /**
     * @brief Records this frame's wireframe of @p physics — every collider
     *        and the last step's contact points (@ref world::debug_lines) —
     *        into @p target's debug-draw list, on top of everything, for
     *        one frame.
     *
     * Call it once per frame the wireframe should show, after the physics
     * steps (the editor does, from its overlay's @c end_frame, while its
     * Helpers panel's Physics toggle is on). Like every debug-draw call it
     * records nothing outside Debug builds.
     */
    void draw_debug(const world& physics, rendering_engine::render_world& target);
} // namespace runtime::physics
