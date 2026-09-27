// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file rigidbody_component.hpp
 * @brief Component that puts a node under rigid-body simulation.
 */

#pragma once

#include <memory>

#include <core/math/vec3.hpp>
#include <runtime/physics/physics_body.hpp>

namespace runtime
{
    struct node;

    /**
     * @brief Makes a node's body static, kinematic or dynamic (see
     *        @ref physics::body_type) with a mass, friction, restitution,
     *        damping and gravity scale.
     *
     * The node's shape comes from its @ref collider_component; without one it
     * collides as a box fitted to the bounds of what the node draws (its mesh
     * or renderable component; a unit box when it draws nothing). The body is
     * created at the start of the next physics step (see @ref physics::world).
     * A dynamic body moves its node: every rendered frame the node's world
     * pose is overwritten with the simulated one (interpolated between the
     * last two fixed steps), and moving the node from game code teleports the
     * body. A kinematic body follows its node and pushes dynamic bodies out of
     * the way.
     *
     * Every setter may be called before or after the component is added to a
     * node; changes reach the simulation at the next step. Velocities,
     * forces and impulses act on dynamic bodies only; forces and impulses
     * accumulate until the next step, which applies and clears them, and the
     * velocity getters report the simulated values after each step.
     *
     * The settings, velocities and listeners live on the heap (a stable
     * address the physics world keeps while the component is relocated in
     * its pool). Disabling the node takes the body out of the simulation;
     * destroying the node or removing the component destroys it.
     */
    struct rigidbody_component
    {
        /** @brief A dynamic body of 1 kg with the default material. */
        rigidbody_component();
        explicit rigidbody_component(physics::body_type type, float mass = 1.0f);
        explicit rigidbody_component(const physics::rigidbody_settings& settings);
        /** @brief Unregisters from the physics world if still registered. */
        ~rigidbody_component();

        rigidbody_component(const rigidbody_component&) = delete;
        rigidbody_component& operator=(const rigidbody_component&) = delete;
        rigidbody_component(rigidbody_component&& other) noexcept = default;
        rigidbody_component& operator=(rigidbody_component&& other) noexcept;

        /** @brief Registers the body with the physics world. */
        void on_attach(node& owner);
        /** @brief Unregisters it; the body is destroyed with the node's last physics component. */
        void on_destroy();
        /** @brief Takes the body out of (or back into) the simulation with its node. */
        void on_active_changed(node& owner, bool active);
        /**
         * @brief A component with the same settings and current velocities,
         *        for @c scene::clone. Listeners are not copied.
         */
        rigidbody_component clone() const;

        physics::rigidbody_settings settings() const;
        void set_settings(const physics::rigidbody_settings& settings);

        physics::body_type type() const;
        /** @brief Changing the type rebuilds the body (its contacts restart). */
        void set_type(physics::body_type type);
        float mass() const;
        void set_mass(float mass);
        float friction() const;
        void set_friction(float friction);
        float restitution() const;
        void set_restitution(float restitution);
        float linear_damping() const;
        void set_linear_damping(float damping);
        float angular_damping() const;
        void set_angular_damping(float damping);
        float gravity_scale() const;
        void set_gravity_scale(float scale);

        /** @brief Linear velocity in m/s, as of the last step (or as last set). */
        core::math::vec3 linear_velocity() const;
        void set_linear_velocity(const core::math::vec3& velocity);
        /** @brief Angular velocity in rad/s about the world axes. */
        core::math::vec3 angular_velocity() const;
        void set_angular_velocity(const core::math::vec3& velocity);

        /** @brief Adds a force (N) through the centre of mass for the next step. */
        void add_force(const core::math::vec3& force);
        /** @brief Adds a force (N) at world-space @p point for the next step. */
        void add_force_at(const core::math::vec3& force, const core::math::vec3& point);
        /** @brief Adds a torque (N m) for the next step. */
        void add_torque(const core::math::vec3& torque);
        /** @brief Applies an impulse (N s) through the centre of mass at the next step. */
        void add_impulse(const core::math::vec3& impulse);
        /** @brief Applies an impulse (N s) at world-space @p point at the next step. */
        void add_impulse_at(const core::math::vec3& impulse, const core::math::vec3& point);
        /** @brief Applies an angular impulse (N m s) at the next step. */
        void add_angular_impulse(const core::math::vec3& impulse);

        /** @brief True when the simulation has put the body to sleep. */
        bool is_sleeping() const;
        /** @brief Wakes the body at the next step. */
        void wake_up();

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
        physics::rigidbody_state& state();
        const physics::rigidbody_state& state() const;
        void release();

        std::unique_ptr<physics::rigidbody_state> m_state;
    };
} // namespace runtime
