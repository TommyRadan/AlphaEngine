// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file physics_body.hpp
 * @brief Settings and per-component state shared by the physics components
 *        and the physics world.
 *
 * Nothing here names the physics library: the settings are plain data in
 * engine math types, and the state structs are what a
 * @ref runtime::rigidbody_component / @ref runtime::collider_component owns on
 * the heap (a stable address the world can point at while the component is
 * relocated within its pool).
 */

#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <core/math/vec3.hpp>
#include <runtime/physics/physics_events.hpp>

namespace runtime
{
    struct node;
} // namespace runtime

namespace runtime::physics
{
    struct world;

    /** @brief How a body moves. */
    enum class body_type
    {
        /** Never moves under simulation; follows its node when the node is moved (a teleport). */
        static_body,
        /** Driven by its node's transform; pushes dynamic bodies but is not pushed back. */
        kinematic_body,
        /** Simulated: gravity, forces and contacts move it, and its node follows. */
        dynamic_body,
    };

    /** @brief Collision shape of a @ref runtime::collider_component. */
    enum class collider_shape
    {
        /** Box of @ref collider_settings::half_extents. */
        box,
        /** Sphere of @ref collider_settings::radius. */
        sphere,
        /**
         * Capsule along the node's local +Z (the engine's up axis): a
         * cylinder of @ref collider_settings::half_height either side of the
         * centre, capped by hemispheres of @ref collider_settings::radius.
         */
        capsule,
        /** Convex hull of @ref collider_settings::points. */
        convex_hull,
    };

    /** @brief Opaque id of a contact listener, for removing it again. */
    using listener_id = std::uint64_t;
    using collision_listener = std::function<void(const collision_event&)>;
    using trigger_listener = std::function<void(const trigger_event&)>;

    /**
     * @brief The collision / trigger listeners registered on one component.
     *
     * Listeners run on the main thread after the physics step that produced
     * the event; see @ref physics_events.hpp for what they may do. They are
     * per instance: cloning a component does not copy them.
     */
    struct contact_listeners
    {
        /** @brief Registers @p listener for collision events; returns its id (0 for a null listener). */
        listener_id add_collision(collision_listener listener);

        /** @brief Registers @p listener for trigger events; returns its id (0 for a null listener). */
        listener_id add_trigger(trigger_listener listener);

        /** @brief Removes the listener with @p id; false when there is none. */
        bool remove(listener_id id);

        std::vector<std::pair<listener_id, collision_listener>> collision;
        std::vector<std::pair<listener_id, trigger_listener>> trigger;
        listener_id next_id{1};
    };

    /**
     * @brief Simulation properties of a @ref runtime::rigidbody_component.
     *
     * Units are SI: kilograms, metres, seconds. A node with a collider but
     * no rigidbody is a static body using these defaults.
     */
    struct rigidbody_settings
    {
        body_type type{body_type::dynamic_body};
        /**
         * @brief Mass in kilograms. Zero or less derives it from the shape's
         *        volume at 1000 kg/m^3 (water). Ignored for static bodies.
         */
        float mass{1.0f};
        /** @brief Coulomb friction coefficient, typically 0..1. */
        float friction{0.5f};
        /** @brief Bounciness, 0 (none) .. 1 (fully elastic). */
        float restitution{0.0f};
        /** @brief Fraction of linear velocity lost per second, 0 .. 1. */
        float linear_damping{0.05f};
        /** @brief Fraction of angular velocity lost per second, 0 .. 1. */
        float angular_damping{0.05f};
        /** @brief Multiplier on the world gravity (0 floats, 1 falls normally). */
        float gravity_scale{1.0f};
    };

    /**
     * @brief Shape and trigger flag of a @ref runtime::collider_component.
     *
     * Dimensions are in the node's local space: the node's world scale is
     * applied on top (a sphere takes the largest axis scale, a capsule the
     * largest of X / Y for its radius and Z for its length). With
     * @ref fit_to_mesh set they are derived instead from the bounds, in the
     * node's local space, of what the node draws — its
     * @ref runtime::mesh_component, else its
     * @ref runtime::renderable_component (a @c premade_3d shape) — whenever
     * it draws something: a box matches the bounds, a sphere takes the
     * largest half extent, a capsule the larger X / Y half extent as radius
     * and the rest of the Z extent as cylinder, and a convex hull the eight
     * corners.
     */
    struct collider_settings
    {
        collider_shape shape{collider_shape::box};
        /** @brief Derive the dimensions from the bounds of what the node draws, when it draws something. */
        bool fit_to_mesh{true};
        core::math::vec3 half_extents{0.5f, 0.5f, 0.5f};
        float radius{0.5f};
        /** @brief Half the length of a capsule's cylindrical section. */
        float half_height{0.5f};
        /** @brief Offset of the shape from the node's origin (box, sphere, capsule). */
        core::math::vec3 center{};
        /** @brief Points of a convex hull; at least four, not all on one plane. */
        std::vector<core::math::vec3> points;
        /**
         * @brief A trigger detects overlaps (reported as @ref trigger_event)
         *        but does not collide.
         */
        bool is_trigger{false};
    };

    /** @brief A force or impulse applied at a world-space point. */
    struct point_force
    {
        core::math::vec3 force{};
        core::math::vec3 point{};
    };

    /**
     * @brief Heap state owned by a @ref runtime::rigidbody_component.
     *
     * The component writes settings, velocities and accumulated forces here;
     * the world reads them before each step and writes the simulated
     * velocities and sleep state back after it. Main-thread only.
     */
    struct rigidbody_state
    {
        rigidbody_settings settings;

        // Current velocities: written by the component (flagging
        // velocity_dirty) and by the world after every step.
        core::math::vec3 linear_velocity{};
        core::math::vec3 angular_velocity{};
        bool velocity_dirty{false};

        // Accumulated since the last step; applied and cleared before the next.
        core::math::vec3 force{};
        core::math::vec3 torque{};
        core::math::vec3 impulse{};
        core::math::vec3 angular_impulse{};
        std::vector<point_force> forces_at;
        std::vector<point_force> impulses_at;
        bool wake_requested{false};

        // Reported by the world after every step.
        bool sleeping{false};

        // The owning node's effective-active state, from on_active_changed.
        bool active{true};

        contact_listeners listeners;

        // Set while registered with a world.
        runtime::node* owner{nullptr};
        world* attached{nullptr};

        /** @brief Drops the accumulated forces, impulses and wake request. */
        void clear_pending();
    };

    /** @brief Heap state owned by a @ref runtime::collider_component. */
    struct collider_state
    {
        collider_settings settings;

        // The owning node's effective-active state, from on_active_changed.
        bool active{true};

        contact_listeners listeners;

        // Set while registered with a world.
        runtime::node* owner{nullptr};
        world* attached{nullptr};
    };
} // namespace runtime::physics
