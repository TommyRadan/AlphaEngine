// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file behavior.hpp
 * @brief Base class for game logic that lives on a scene node.
 *
 * A @ref runtime::behavior is a piece of game logic owned by the world: an
 * object attached to a @ref runtime::node (through a
 * @ref runtime::behavior_component) whose state lives in its member fields
 * and whose code runs from lifecycle hooks the engine calls. Destroying the
 * node, removing the component or unloading the scene destroys it, so it can
 * never outlive the subsystems it talks to.
 *
 * **Lifecycle.** For one behaviour, in order:
 *
 * - @ref behavior::on_enable — when it is attached to a node that is
 *   effectively active, and whenever its node becomes effectively active
 *   again (see @ref node::set_active).
 * - @ref behavior::on_start — once, right before the first
 *   @ref behavior::on_fixed_update or @ref behavior::on_update it receives.
 * - @ref behavior::on_fixed_update — once per fixed simulation step.
 * - @ref behavior::on_update — once per rendered frame.
 * - @ref behavior::on_disable — whenever its node stops being effectively
 *   active, and before @ref behavior::on_destroy if it is enabled then.
 * - @ref behavior::on_destroy — once, when its component is destroyed;
 *   the behaviour itself is deleted right after.
 *
 * Only an enabled behaviour is updated, and none is updated once its node has
 * been queued for destruction.
 *
 * **Order within an engine tick** (@ref runtime::engine::tick):
 *
 * 1. The window pumps input; @c core input events reach their event-bus
 *    listeners. A behaviour that wants input subscribes to those events (in
 *    @ref behavior::on_enable, say) and keeps the tokens as members.
 * 2. For every fixed step drained this frame, @c core::frame is emitted and
 *    every enabled behaviour's @ref behavior::on_fixed_update runs from it,
 *    with the other @c core::frame listeners, in the order the behaviours
 *    were attached. A frame may run zero, one or several fixed steps.
 * 3. @c core::render_update is emitted to its listeners.
 * 4. Every loaded scene updates (the persistent scene first, then load
 *    order): world transforms settle, then @c on_update runs one component
 *    type at a time, which is where every enabled behaviour's
 *    @ref behavior::on_update runs, parents before children. The scene then
 *    applies its deferred commands.
 * 5. The frame is drawn.
 *
 * Both deltas are in milliseconds, like the @c core::frame and
 * @c core::render_update events they mirror: the fixed step length, and the
 * real time since the previous rendered frame (zero on the first frame).
 * Use @ref behavior::on_update for smooth, render-rate motion (a camera, a
 * spinning prop) and @ref behavior::on_fixed_update for frame-rate
 * independent simulation.
 *
 * **Changing the scene from a hook.** @ref behavior::on_start,
 * @ref behavior::on_fixed_update, @ref behavior::on_update,
 * @ref behavior::on_enable and @ref behavior::on_disable can run while the
 * scene is being walked, so they follow the scene's rule for component hooks:
 * the immediate structural node APIs are refused there, and changes go
 * through the scene's deferred commands. In particular a behaviour destroys
 * its own node with
 * @code
 * owner().scene()->destroy_node(owner());
 * @endcode
 * which is applied at the end of the scene's update: the behaviour receives
 * no further updates meanwhile, then gets its @ref behavior::on_disable and
 * @ref behavior::on_destroy from the teardown.
 *
 * Main-thread-only, like the rest of the scene. Hooks must not throw.
 */

#pragma once

#include <cassert>
#include <memory>

namespace runtime
{
    struct node;
    struct behavior_component;

    /**
     * @brief Game logic attached to a node: the owning node, plus virtual
     *        lifecycle hooks that default to doing nothing.
     *
     * Derive from it, keep the object's state in members, override the hooks
     * the logic needs, and attach an instance with
     * @ref runtime::add_behavior (or
     * @c node.add_component(behavior_component{...})). A node holds at most
     * one behaviour, as it holds at most one component of each type; a second
     * piece of logic goes on a child node. See the file notes for when each
     * hook runs.
     *
     * **Cloning.** @c scene::clone copies a behaviour only if its type
     * opts in by overriding @ref clone; the default declines, and the copy
     * of the node is left without a behaviour (with a warning). The usual
     * implementation is @c std::make_unique<my_behavior>(*this): copying a
     * behaviour copies the derived state only, and the copy gets its own
     * owner, @ref on_enable and @ref on_start once it is attached.
     */
    struct behavior
    {
        virtual ~behavior() = default;

        /**
         * @brief The node this behaviour is attached to.
         *
         * Valid from attachment — so in every hook — until the behaviour is
         * destroyed. Not to be called before the behaviour is attached.
         */
        node& owner() const noexcept
        {
            assert(m_owner != nullptr && "runtime::behavior::owner: not attached to a node");
            return *m_owner;
        }

        /** @brief True between @ref on_enable and @ref on_disable. */
        bool is_enabled() const noexcept
        {
            return m_enabled;
        }

        /** @brief True once @ref on_start has run. */
        bool is_started() const noexcept
        {
            return m_started;
        }

        /** @brief The behaviour became active: attached to an active node, or its node was re-enabled. */
        virtual void on_enable() {}

        /** @brief Runs once, before the behaviour's first fixed step or frame update. */
        virtual void on_start() {}

        /**
         * @brief One fixed simulation step.
         * @param delta_time Length of the step, in milliseconds.
         */
        virtual void on_fixed_update(float delta_time)
        {
            (void)delta_time;
        }

        /**
         * @brief One rendered frame.
         * @param delta_time Real time since the previous rendered frame, in milliseconds.
         */
        virtual void on_update(float delta_time)
        {
            (void)delta_time;
        }

        /** @brief The behaviour stopped being active: its node was disabled, or it is being destroyed. */
        virtual void on_disable() {}

        /** @brief The behaviour's component is being destroyed; it is deleted right after this returns. */
        virtual void on_destroy() {}

        /**
         * @brief A fresh copy of this behaviour for @c scene::clone, or
         *        @c nullptr (the default) when the type cannot be cloned.
         */
        virtual std::unique_ptr<behavior> clone() const
        {
            return nullptr;
        }

    protected:
        behavior() = default;

        // A copy is a new, unattached instance: the lifecycle state below is
        // per instance and is never copied, so a copy made for clone() gets
        // its own owner, on_enable and on_start.
        behavior(const behavior& other) noexcept
        {
            (void)other;
        }

        behavior& operator=(const behavior& other) noexcept
        {
            (void)other;
            return *this;
        }

    private:
        // The adapter drives the lifecycle state.
        friend struct behavior_component;

        node* m_owner{nullptr};
        bool m_enabled{false};
        bool m_started{false};
    };
} // namespace runtime
