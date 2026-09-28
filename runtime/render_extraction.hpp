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
     * render representation — @ref light_component, @ref camera_component,
     * @ref mesh_component and @ref renderable_component — hands every
     * component its node, and the component writes its proxy: a light's or
     * camera's settings every frame; a pose only when the node's world
     * matrix (and, for a camera, its offset) moved since the last
     * extraction, so a static object costs no matrix work; a mesh source's
     * description, with an instanced source's changed instance records and
     * draw arguments, only when the source changed; and a skinned mesh's
     * joint palette only when a new one was set. The renderer reads these
     * copies (and the materials and mesh assets they name), never a
     * component, a mesh source or a node.
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
