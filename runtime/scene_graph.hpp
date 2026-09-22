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

#include <cstddef>
#include <functional>
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
     *
     * **Deferred commands.** The per-frame @ref update walks the tree and
     * dispatches component hooks; while it does, the node and child lists it
     * iterates must not change. Any structural change a hook wants — destroy
     * the node it runs on, drop a component, move a node, disable a subtree —
     * is queued with @ref defer_destroy, @ref defer_remove_component,
     * @ref defer_reparent or @ref defer_set_active (or the raw @ref defer)
     * and applied, in the order queued, once the walk has finished. Commands
     * queued outside a traversal are applied at the end of the next
     * @ref update as well, or immediately by @ref apply_deferred.
     */
    struct context
    {
        /**
         * @brief RAII marker for a traversal in progress.
         *
         * Constructed by @ref node::update_subtree and the active-state
         * refresh around their hook dispatch, so that while any such walk is
         * on the stack @ref is_traversing is true and the node's immediate
         * structural APIs refuse (debug) or defer (release). Nesting is fine;
         * a null scene makes it a no-op.
         */
        struct traversal_scope
        {
            explicit traversal_scope(context* scene) noexcept;
            ~traversal_scope();

            traversal_scope(const traversal_scope&) = delete;
            traversal_scope& operator=(const traversal_scope&) = delete;

        private:
            context* m_scene;
        };

    private:
        // Declared before components and root so they are destroyed after
        // them: the root's destructor orphans its children, whose active-state
        // refresh may still mark a traversal or queue a command here.
        int m_traversal_depth{0};
        std::vector<std::function<void()>> m_pending;

    public:
        context();
        ~context();

        /** @brief Initializes the scene graph subsystem. */
        void init();

        /** @brief Shuts down the scene graph subsystem. */
        void quit();

        /**
         * @brief Advances the scene one frame: propagates component updates,
         *        then applies every deferred command.
         *
         * Walks the node tree from @ref root and dispatches each component's
         * @c on_update so node-derived state (light positions, camera poses)
         * tracks the hierarchy, then drains the deferred command queue. Called
         * from @ref runtime::engine::tick after game-module @c on_frame and
         * before the renderer draws.
         */
        void update();

        // --- Deferred commands ---------------------------------------------

        /**
         * @brief Queues an arbitrary command for the end of the current (or
         *        next) @ref update.
         *
         * The typed helpers below are built on this. A command may queue
         * further commands; they run in the same drain.
         */
        void defer(std::function<void()> command);

        /**
         * @brief Queues the destruction of @p target: detach it from its
         *        parent, free every component on it and its descendants
         *        (dispatching @c on_destroy), then invoke @p release.
         *
         * Nodes are caller-owned, so the scene cannot free the memory itself;
         * @p release is where the owner does that (e.g. erase the
         * @c unique_ptr holding the node). The node is not touched after
         * @p release runs, so deleting it there is safe. Without a release
         * callback the node survives as a detached, component-less subtree
         * the owner may reuse or delete later. @p target must stay alive until
         * the command has run — its @ref node::is_destroy_pending reports
         * @c true in the meantime — and a second call for a node already
         * pending is ignored.
         */
        void defer_destroy(node& target, std::function<void()> release = {});

        /** @brief Queues @c target.remove_component<C>() for the end of the update. */
        template<typename C>
        void defer_remove_component(node& target)
        {
            defer([&target] { target.remove_component<C>(); });
        }

        /**
         * @brief Queues a re-parent of @p target under @p new_parent, or a
         *        detach to world space when @p new_parent is @c nullptr.
         *
         * Applied through @ref node::add / @ref node::remove, so an ancestor
         * cycle is rejected (with an error logged) at that point.
         */
        void defer_reparent(node& target, node* new_parent);

        /** @brief Queues @c target.set_active(active) for the end of the update. */
        void defer_set_active(node& target, bool active);

        /**
         * @brief Applies every queued command now, in queue order, including
         *        any a command queues while running.
         *
         * Called by @ref update once the traversal has finished; callable
         * directly from outside a traversal (e.g. after a batch of explicit
         * deferrals). Logs an error and leaves the queue untouched if a
         * traversal is in progress.
         */
        void apply_deferred();

        /** @brief Number of commands waiting to be applied. */
        std::size_t pending_command_count() const noexcept;

        /** @brief True while a tree walk that dispatches component hooks is on the stack. */
        bool is_traversing() const noexcept;

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
    };
} // namespace runtime
