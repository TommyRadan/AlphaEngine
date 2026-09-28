// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file physics_world.hpp
 * @brief The engine's physics subsystem: a rigid-body world stepped on the
 *        fixed timestep.
 *
 * The simulation is Jolt Physics, but no Jolt type appears in this header
 * or any other engine header — the library is included only by the
 * implementation (physics_world.cpp), the way GLM is confined to
 * core/math. Everything here speaks @ref core::math types.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <core/math/ray.hpp>
#include <core/math/vec3.hpp>
#include <runtime/physics/physics_body.hpp>

namespace runtime
{
    struct node;
} // namespace runtime

namespace runtime::physics
{
    /** @brief The closest body a @ref world::raycast hit. */
    struct raycast_hit
    {
        /** @brief The node whose rigidbody / collider was hit. */
        runtime::node* node{nullptr};
        /** @brief World-space hit point. */
        core::math::vec3 point{};
        /** @brief World-space surface normal at @ref point. */
        core::math::vec3 normal{};
        /** @brief Distance from the ray origin to @ref point. */
        float distance{0.0f};
        /** @brief True when the hit body is a trigger. */
        bool trigger{false};
    };

    /**
     * @brief Rigid-body physics world, owned by @ref runtime::engine as
     *        @c engine::physics with the usual @ref init / @ref quit shape.
     *
     * **Bodies.** A node carrying a @ref runtime::collider_component and/or a
     * @ref runtime::rigidbody_component gets one body: the collider supplies
     * the shape (a rigidbody alone gets a box fitted to what the node draws,
     * or a unit box) and the rigidbody the motion (a collider alone is a
     * static body). The components register their heap state with the world
     * on attach and unregister on destroy; the body itself is created lazily,
     * at the start of the next @ref step, so components can be added in any
     * order. Disabling the node (@c on_active_changed) takes the body out of
     * the simulation at once and enabling it puts it back.
     *
     * **Stepping.** @ref step runs once per fixed update, after that step's
     * game logic (the engine runs it in the scheduler's @c physics stage,
     * right after @c scripts_fixed; runtime/scheduler.hpp), so it stops
     * while the game is paused. Before simulating it pushes the scene into
     * the world: kinematic bodies are driven towards their node's pose, a
     * static or dynamic body whose node was moved by game code is
     * teleported, and shape, material, velocity and force changes are
     * applied. After simulating it records every awake dynamic body's new
     * pose. A node destroyed during a step's game logic takes its body out
     * when the step's deferred commands are applied, before the next step.
     *
     * **Interpolation.** Once per rendered frame, after the fixed steps (at
     * the start of the scheduler's @c update stage, skipped while the game is
     * paused so tools can move a body's node meanwhile), @ref interpolate
     * writes each moving dynamic body's pose to its node,
     * @c core::time::interpolation_alpha of the way from the previous step's
     * pose to the latest one, so motion stays smooth when the render rate
     * runs ahead of the fixed rate (the pose shown trails the simulation by
     * under one step, as with the animator). Poses are written in world space
     * and are exact when the node's ancestors are unscaled or uniformly
     * scaled.
     *
     * **Events.** The library reports contacts from its worker threads;
     * they are buffered during the step and turned into
     * @ref collision_event / @ref trigger_event enter / stay / exit on the
     * main thread once it returns, then emitted on the event bus and handed
     * to the per-component listeners (see physics_events.hpp).
     *
     * **Threads.** The simulation runs on a small pool of physics worker
     * threads (half the hardware threads, between one and four) plus the
     * calling thread; every member function is main-thread only.
     *
     * **Debug draw.** @ref debug_lines describes every collider — green
     * dynamic (dim when asleep), blue kinematic, grey static, yellow
     * trigger — and the last step's contact points in red. Debug builds
     * draw it on top of the scene through the debug-draw functions
     * (@ref draw_debug, physics_debug_draw.hpp), which the editor calls
     * every frame while its Helpers panel's Physics toggle is on. The world
     * itself does not depend on the renderer.
     */
    struct world
    {
        world();
        ~world();

        world(const world&) = delete;
        world& operator=(const world&) = delete;
        world(world&&) = delete;
        world& operator=(world&&) = delete;

        /**
         * @brief Brings the physics library and the simulation up.
         *
         * Needs the event bus to be up already.
         */
        void init();

        /**
         * @brief Destroys every body and shuts the simulation down.
         *
         * Components still registered are cut loose (their later
         * @c on_destroy is a no-op). Safe when @ref init never ran or failed
         * partway; called again by the destructor.
         */
        void quit();

        /** @brief True between a successful @ref init and @ref quit. */
        bool is_initialized() const noexcept;

        /**
         * @brief Advances the simulation by @p delta_seconds: syncs the scene
         *        in, simulates, writes the dynamic bodies back and dispatches
         *        the contact events. A no-op before @ref init.
         */
        void step(double delta_seconds);

        /**
         * @brief Moves every dynamic body's node to its pose @p alpha (0..1) of
         *        the way from the previous step to the latest one. Called once
         *        per rendered frame, after the fixed steps, with
         *        @c core::time::interpolation_alpha.
         */
        void interpolate(float alpha);

        /**
         * @brief The closest body along @p ray within @p max_distance.
         *
         * The ray direction need not be normalised. Triggers are skipped
         * unless @p include_triggers is set. Sees the bodies as of the last
         * step: one added since then is not in the world yet.
         */
        std::optional<raycast_hit>
        raycast(const core::math::ray& ray, float max_distance, bool include_triggers = false) const;

        /** @brief World gravity in m/s^2; defaults to 9.81 down the engine's -Z. */
        void set_gravity(const core::math::vec3& gravity);
        core::math::vec3 gravity() const noexcept;

        /** @brief Number of bodies currently in the simulation. */
        std::size_t body_count() const noexcept;

        /**
         * @brief Appends the debug wireframe — every collider plus the last
         *        step's contact points — as line-segment pairs.
         */
        void debug_lines(std::vector<core::math::vec3>& positions, std::vector<core::math::vec3>& colors) const;

        /** @brief Bumped whenever the debug wireframe may have changed (every step, every body change). */
        std::uint64_t revision() const noexcept;

        // Called by rigidbody_component / collider_component from their
        // hooks; not meant for game code.

        /** @brief Registers @p state as the rigidbody of @p owner. */
        void attach(runtime::node& owner, rigidbody_state& state);
        /** @brief Registers @p state as the collider of @p owner. */
        void attach(runtime::node& owner, collider_state& state);
        /** @brief Unregisters @p state; the body goes when its node has neither component left. */
        void detach(rigidbody_state& state);
        /** @brief Unregisters @p state; the body goes when its node has neither component left. */
        void detach(collider_state& state);
        /** @brief Adds or removes @p owner's body after a component's active flag changed. */
        void refresh_active(runtime::node& owner);

    private:
        struct impl;
        std::unique_ptr<impl> m_impl;
    };
} // namespace runtime::physics
