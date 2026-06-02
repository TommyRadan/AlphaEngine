/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file scene_graph.hpp
 * @brief Scene graph subsystem entry point.
 */

#pragma once

#include <string>
#include <vector>

#include <runtime/component.hpp>
#include <runtime/node.hpp>

namespace runtime
{
    /**
     * @brief Lifetime owner of the scene graph subsystem.
     *
     * Owned by @ref runtime::engine. Brings the subsystem up and down with the
     * same @ref init / @ref quit shape as the other subsystems, and owns both
     * the @ref components store (the per-scene component pools) and the
     * @ref root of the node hierarchy — the world-space anchor every other
     * @ref node parents under (directly or transitively).
     */
    struct context
    {
        context();

        /** @brief Initializes the scene graph subsystem. */
        void init();

        /** @brief Shuts down the scene graph subsystem. */
        void quit();

        /**
         * @brief Advances the scene one frame: propagates component updates.
         *
         * Walks the node tree from @ref root and dispatches each component's
         * @c on_update so node-derived state (light positions, camera poses)
         * tracks the hierarchy. Called from @ref runtime::engine::tick after
         * game-module @c on_frame and before the renderer draws.
         */
        void update();

        /**
         * @brief Pools backing every node's components.
         *
         * One @ref core::pool per component type; nodes hold handles
         * into it rather than owning component data. Declared before @ref root
         * so it outlives the node tree and is still alive when nodes free their
         * components during teardown.
         */
        component_store components;

        /**
         * @brief Root of the scene hierarchy.
         *
         * Sits at world origin with identity transform and is wired to
         * @ref components, so nodes added under it (directly or transitively)
         * inherit the store and can carry components. It has no special
         * behaviour beyond being a conventional, always-present parent.
         */
        node root;

    private:
        // Reusable scratch for the breadth-first world-matrix prepass in
        // @ref update: the active nodes at the current depth and the next one.
        // Kept as members so the per-frame walk reuses one allocation that
        // grows to the scene's high-water mark rather than allocating each tick.
        std::vector<node*> m_band;
        std::vector<node*> m_next_band;
    };
} // namespace runtime
