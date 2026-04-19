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
 * @file scene_manager.hpp
 * @brief Stack-based owner of @ref scene instances.
 */

#pragma once

#include <memory>
#include <vector>

#include <scene_graph/scene.hpp>

namespace scene_graph
{
    /**
     * @brief Owning stack of scenes with push / pop / replace semantics.
     *
     * The top of the stack is the @ref active scene — the one that
     * receives @ref scene::on_update from the main loop. Lower scenes
     * remain loaded but frozen, which makes it cheap to overlay e.g. a
     * pause menu over a running gameplay scene and pop back to it.
     *
     * Ownership is transferred to the manager via @c std::unique_ptr:
     * callers construct a concrete @ref scene subclass and hand the
     * owned pointer to @ref push or @ref replace. Not thread-safe —
     * call only from the main loop thread.
     */
    class scene_manager
    {
    public:
        /**
         * @brief Pushes @p next onto the stack and calls @c on_load on it.
         *
         * The previously active scene remains on the stack but stops
         * receiving @c on_update. Typical use: overlay a pause menu
         * while the gameplay scene stays loaded underneath.
         *
         * @param next The scene to push. Must not be null.
         */
        void push(std::unique_ptr<scene> next);

        /**
         * @brief Replaces the top of the stack with @p next.
         *
         * If a scene is already active, its @c on_unload is called and
         * it is destroyed before @p next is pushed and loaded. Typical
         * use: transition between gameplay scenes (e.g. level change).
         *
         * @param next The scene to make active. Must not be null.
         */
        void replace(std::unique_ptr<scene> next);

        /**
         * @brief Pops the top of the stack.
         *
         * Calls @c on_unload on the popped scene and destroys it. The
         * scene below (if any) becomes active again. No-op when the
         * stack is empty.
         */
        void pop();

        /**
         * @brief Returns the active (top-of-stack) scene.
         *
         * Throws @c std::runtime_error if the stack is empty — callers
         * should check @ref has_active first when the stack may be empty.
         */
        scene& active();

        /** @brief Returns @c true when at least one scene is on the stack. */
        bool has_active() const;

        /** @brief Number of scenes currently on the stack. */
        std::size_t size() const;

    private:
        std::vector<std::unique_ptr<scene>> m_stack;
    };
} // namespace scene_graph
