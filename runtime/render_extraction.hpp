// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_extraction.hpp
 * @brief The once-per-frame step that copies the world's render state into
 *        the render_world the renderer draws from.
 */

#pragma once

namespace runtime
{
    struct scene_manager;

    /**
     * @brief Writes the render representation of every loaded scene into
     *        the @ref rendering_engine::render_world that scene feeds.
     *
     * For each loaded scene, one pass over each component pool with a
     * render representation — @ref light_component and
     * @ref camera_component — hands every component its node, and the
     * component writes its proxy: its settings every frame, and its pose
     * only when the node's world matrix (and, for a camera, its offset)
     * moved since the last extraction, so a static light or camera costs no
     * matrix work.
     *
     * @ref runtime::engine::tick calls it once per rendered frame, after
     * every update that can move a node — the fixed steps, physics and its
     * interpolation, the scene update with its deferred commands, and the
     * overlay's editing — and right before @c renderer::render. Every pose
     * the renderer reads therefore comes from the same, final state of the
     * frame, whatever order the component types update in. Proxies are
     * created, enabled and destroyed by the components' own hooks, not here,
     * so their order is the order those hooks ran in.
     */
    void extract_render_proxies(scene_manager& scenes);
} // namespace runtime
