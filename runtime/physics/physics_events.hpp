// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file physics_events.hpp
 * @brief Contact and trigger events reported by the physics subsystem.
 *
 * Both are plain value types. The physics world emits each one on the
 * engine's @ref core::event_bus and hands it to the per-component listener
 * lists of the nodes involved (see @ref runtime::rigidbody_component::on_collision
 * and friends). Everything is delivered on the main thread, after the fixed
 * step that produced it and outside the scene update, so a listener may
 * queue structural changes — destroy a node, re-parent one, drop a component
 * — through the scene's deferred commands (@c scene::destroy_node,
 * @c defer_reparent, @c defer_remove_component).
 */

#pragma once

#include <core/math/vec3.hpp>

namespace runtime
{
    struct node;
} // namespace runtime

namespace runtime::physics
{
    /**
     * @brief Where a contact is in its lifetime.
     *
     * @c enter fires on the first step two bodies touch (or overlap, for a
     * trigger), @c stay on every later step they still do while at least one
     * of them is awake, and @c exit on the first step they no longer do. A
     * pair whose bodies fall asleep while touching stays in contact without
     * reporting @c stay, and continues silently when they wake still
     * touching. A body removed from the simulation (its node disabled)
     * reports @c exit; one destroyed (its component or node freed) is
     * dropped from its contacts without an @c exit, since its node may no
     * longer exist.
     */
    enum class contact_phase
    {
        enter,
        stay,
        exit,
    };

    /**
     * @brief Two solid (non-trigger) bodies touching.
     *
     * On the event bus one event is emitted per body pair, @ref self and
     * @ref other in a stable but arbitrary order. A per-component listener
     * receives it oriented to its own node: @ref self is always the node the
     * listening component sits on.
     */
    struct collision_event
    {
        contact_phase phase{contact_phase::enter};
        /** @brief The node on whose behalf the event is reported (see the struct note). */
        runtime::node* self{nullptr};
        /** @brief The node it touches. */
        runtime::node* other{nullptr};
        /**
         * @brief A world-space contact point on @ref self's surface. For
         *        @c exit, the last point seen while in contact.
         */
        core::math::vec3 point{};
        /**
         * @brief World-space contact normal pointing from @ref self towards
         *        @ref other. For @c exit, the last normal seen.
         */
        core::math::vec3 normal{};
    };

    /**
     * @brief A body overlapping a trigger collider.
     *
     * Delivered unchanged to both parties' listeners: @ref trigger is the
     * node whose collider is the trigger, @ref other the node overlapping it.
     * Two triggers never report against each other.
     */
    struct trigger_event
    {
        contact_phase phase{contact_phase::enter};
        runtime::node* trigger{nullptr};
        runtime::node* other{nullptr};
    };
} // namespace runtime::physics
