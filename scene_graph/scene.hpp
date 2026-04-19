/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file scene.hpp
 * @brief Base class for scenes managed by the @ref scene_manager stack.
 */

#pragma once

#include <scene_graph/world.hpp>

namespace scene_graph
{
    /**
     * @brief Polymorphic base for a single gameplay scene.
     *
     * Scenes are owned by the @ref scene_manager through
     * @c std::unique_ptr and invoked through the three lifecycle hooks
     * below. Every scene carries its own @ref world — the ECS world is
     * currently a placeholder (see @c world.hpp) and will be replaced
     * once issue #16 lands.
     *
     * Subclasses override the hooks they care about; default
     * implementations are no-ops so trivial scenes can opt in
     * selectively. Lifecycle ordering:
     *   - @ref on_load is called when the scene is pushed onto the
     *     stack (via @ref scene_manager::push or @ref scene_manager::replace).
     *   - @ref on_update is driven from the main loop for the active
     *     (top-of-stack) scene only.
     *   - @ref on_unload is called when the scene is popped or replaced.
     */
    class scene
    {
    public:
        virtual ~scene() = default;

        /** @brief Called once immediately after the scene is pushed onto the stack. */
        virtual void on_load() {}

        /** @brief Called once immediately before the scene is removed from the stack. */
        virtual void on_unload() {}

        /**
         * @brief Called once per frame for the active scene.
         * @param dt Time elapsed since the previous frame, in seconds.
         */
        virtual void on_update(float dt)
        {
            (void)dt;
        }

        /**
         * @brief Per-scene ECS world.
         *
         * Placeholder until issue #16 (ECS world) is merged — see
         * @c world.hpp for the rationale.
         */
        world game_world;
    };
} // namespace scene_graph
