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
 * been queued for destruction or while the game is paused (time scale 0).
 *
 * **Order within a frame** (the stages of @ref runtime::scheduler):
 *
 * 1. @c input — the window pumps input; @c core input events reach their
 *    event-bus listeners. A behaviour that wants input subscribes to those
 *    events (in @ref behavior::on_enable, say) and keeps the tokens as
 *    members, or polls @c core::input from its hooks.
 * 2. The fixed stage, once per fixed step drained this frame (zero, one or
 *    several): @c scripts_fixed runs every enabled behaviour's
 *    @ref behavior::on_fixed_update, in hierarchy order — every loaded scene
 *    in turn (the persistent scene first, then load order), and within a
 *    scene depth-first, parents before children, siblings in the order they
 *    were added — whatever order the behaviours were attached in; then
 *    @c physics steps the simulation and @c post_physics follows it; then
 *    the scenes apply their deferred commands, so a node a behaviour
 *    destroyed is gone, rigid body included, before the next step.
 * 3. @c update — every enabled behaviour's @ref behavior::on_update, in the
 *    same hierarchy order; then the deferred commands again.
 * 4. @c animation, @c transform_propagation, @c audio, @c render_extract;
 *    then the frame is drawn.
 *
 * Both deltas are in seconds of game time: the fixed step length
 * (@c core::time::fixed_delta_time), and the time since the previous
 * rendered frame scaled by the time scale (@c core::time::delta_time; zero
 * on the first frame). Use @ref behavior::on_update for smooth, render-rate
 * motion (a camera, a spinning prop) and @ref behavior::on_fixed_update for
 * frame-rate independent simulation; the time scale slows both down, since
 * it thins out the fixed steps and scales the frame delta.
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
 * which is applied when the stage that queued it finishes (after the fixed
 * step, or after the update): the behaviour receives no further updates
 * meanwhile, then gets its @ref behavior::on_disable and
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
         * @param delta_time Length of the step, in seconds.
         */
        virtual void on_fixed_update(double delta_time)
        {
            (void)delta_time;
        }

        /**
         * @brief One rendered frame.
         * @param delta_time Game time since the previous rendered frame (the real time times the time scale), in
         *                   seconds.
         */
        virtual void on_update(double delta_time)
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
