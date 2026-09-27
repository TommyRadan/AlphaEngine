/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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
 * @file collider_component.hpp
 * @brief Component that gives a node a collision shape.
 */

#pragma once

#include <memory>
#include <vector>

#include <core/math/vec3.hpp>
#include <runtime/physics/physics_body.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Gives a node a collision shape — box, sphere, capsule or convex
     *        hull (see @ref physics::collider_shape) — optionally as a trigger.
     *
     * On its own it makes the node a static body; with a
     * @ref rigidbody_component on the same node it shapes that body. The
     * default component is a box fitted to the bounds of what the node draws
     * — its mesh or renderable component (a unit box when it draws nothing) —
     * and @ref fitted gives any other shape the same treatment; the explicit
     * factories take their dimensions as given, in the node's local space.
     * The node's world scale applies on top, and a changed scale, mesh or
     * dimension reshapes the body at the next step.
     *
     * A trigger reports overlaps as @ref physics::trigger_event instead of
     * colliding. Contact listeners registered here receive the events of the
     * node's body, as those on its rigidbody do.
     *
     * The settings and listeners live on the heap, like the rigidbody's; the
     * body goes when the node has neither component left, and leaves the
     * simulation while the node is disabled.
     */
    struct collider_component
    {
        /** @brief A box fitted to what the node draws (a unit box when it draws nothing). */
        collider_component();
        explicit collider_component(const physics::collider_settings& settings);
        /** @brief Unregisters from the physics world if still registered. */
        ~collider_component();

        collider_component(const collider_component&) = delete;
        collider_component& operator=(const collider_component&) = delete;
        collider_component(collider_component&& other) noexcept = default;
        collider_component& operator=(collider_component&& other) noexcept;

        /** @brief A box of @p half_extents centred at @p center. */
        static collider_component box(const core::math::vec3& half_extents, const core::math::vec3& center = {});
        /** @brief A sphere of @p radius centred at @p center. */
        static collider_component sphere(float radius, const core::math::vec3& center = {});
        /**
         * @brief A capsule along the local +Z axis: a cylinder reaching
         *        @p half_height either side of @p center, capped by
         *        hemispheres of @p radius.
         */
        static collider_component capsule(float radius, float half_height, const core::math::vec3& center = {});
        /** @brief The convex hull of @p points (at least four, not all on one plane). */
        static collider_component convex_hull(std::vector<core::math::vec3> points);
        /**
         * @brief A @p shape fitted to the bounds of what the node draws
         *        (see @ref physics::collider_settings); a convex hull
         *        fitted this way is the hull of the bounds' eight corners.
         */
        static collider_component fitted(physics::collider_shape shape);

        // --- Component hooks ------------------------------------------------

        /** @brief Registers the shape with the physics world. */
        void on_attach(node& owner);
        /** @brief Unregisters it; the body is destroyed with the node's last physics component. */
        void on_destroy();
        /** @brief Takes the body out of (or back into) the simulation with its node. */
        void on_active_changed(node& owner, bool active);
        /** @brief A component with the same settings, for @c scene::clone. Listeners are not copied. */
        collider_component clone() const;

        // --- Settings -------------------------------------------------------

        physics::collider_settings settings() const;
        /** @brief Replaces the shape and trigger flag; applied at the next step. */
        void set_settings(const physics::collider_settings& settings);

        physics::collider_shape shape() const;
        bool is_trigger() const;
        /** @brief Turning a collider into a trigger (or back) rebuilds the body. */
        void set_trigger(bool trigger);

        // --- Contact listeners ----------------------------------------------

        /**
         * @brief Calls @p listener for every collision this node's body is in,
         *        with @ref physics::collision_event::self set to this node.
         */
        physics::listener_id on_collision(physics::collision_listener listener);
        /** @brief Calls @p listener for every trigger overlap this node's body is in. */
        physics::listener_id on_trigger(physics::trigger_listener listener);
        /** @brief Removes a listener added by @ref on_collision / @ref on_trigger. */
        bool remove_listener(physics::listener_id id);

    private:
        // The heap state; recreated if this component was moved from.
        physics::collider_state& state();
        const physics::collider_state& state() const;
        void release();

        std::unique_ptr<physics::collider_state> m_state;
    };
} // namespace runtime
