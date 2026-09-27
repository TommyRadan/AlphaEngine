// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file engine.hpp
 * @brief Central, owning container for every engine subsystem.
 *
 * @ref engine owns each subsystem as a @c std::unique_ptr so
 * construction/destruction order is explicit and deterministic. One live
 * instance is published through @ref current_engine so a call site can
 * resolve the subsystem it needs without threading an @c engine& through
 * every function.
 */

#pragma once

#include <memory>

#include <core/subscription.hpp>

// Forward declarations keep this header lightweight. Subsystem headers
// are included only in engine.cpp where the unique_ptrs are constructed
// and destroyed.
namespace runtime
{
    struct engine_settings;
}
namespace core
{
    struct event_bus;
}
namespace core
{
    struct input;
}
namespace core
{
    struct job_pool;
}
namespace core
{
    struct time;
}
namespace core
{
    struct audio;
}
namespace platform
{
    struct window;
}
namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }
    struct asset_cache;
    struct renderer;
} // namespace rendering_engine
namespace runtime
{
    struct overlay;
    struct scene_manager;
} // namespace runtime
namespace runtime
{
    struct script_host;
}
namespace runtime::physics
{
    struct debug_draw;
    struct world;
} // namespace runtime::physics

namespace runtime
{
    /**
     * @brief Owning container for every engine subsystem.
     *
     * The constructor wires up each subsystem and installs itself as
     * the process-wide @ref current_engine; the destructor tears
     * subsystems down in reverse and clears the current-engine pointer.
     * Not copyable and not moveable — the engine publishes its address
     * through @ref current_engine and moving would invalidate that
     * pointer.
     *
     * All methods are main-thread-only. Member access is not
     * synchronised.
     */
    struct engine
    {
        /**
         * @brief Wires every subsystem up around the resolved @p values
         *        (see @ref runtime::register_engine_settings and @ref core::load_settings) and installs itself
         *        as @ref current_engine. @c runtime::engine_settings{} gives the compiled defaults.
         */
        explicit engine(engine_settings values);
        ~engine();

        engine(const engine&) = delete;
        engine& operator=(const engine&) = delete;
        engine(engine&&) = delete;
        engine& operator=(engine&&) = delete;

        /**
         * @brief Initializes every subsystem in dependency order — the
         *        window, then the GPU device against it, then the renderer —
         *        then installs the game modules (runtime/game_module.hpp)
         *        into the active scene.
         */
        void init();

        /** @brief Tears every subsystem down in reverse order. */
        void quit();

        /**
         * @brief Runs one iteration of the main loop: pumps input,
         *        renders one frame, advances the clock.
         */
        void tick();

        /** @brief Broadcasts @c engine_start on the event bus. */
        void broadcast_engine_start();

        /** @brief Broadcasts @c engine_stop on the event bus. */
        void broadcast_engine_stop();

        /** @brief Returns true when a @c quit_requested event has been observed. */
        bool is_quit_requested() const noexcept;

        /**
         * @brief Installs @p value, the tool layer drawn over every
         *        rendered frame (the debug editor), replacing any previous
         *        one; null installs none. Call before @ref init. The engine
         *        owns it from then on and drives it as @ref overlay
         *        describes.
         */
        void set_overlay(std::unique_ptr<overlay> value);

        // Subsystems. Owned as unique_ptr so lifetime mirrors the
        // engine's own lifetime, in the order they are declared here.
        std::unique_ptr<engine_settings> settings;
        std::unique_ptr<core::time> time;
        std::unique_ptr<core::job_pool> jobs;
        std::unique_ptr<core::event_bus> events;
        // No rendering dependency (the platform's playback device, opened
        // independently of the window), so it lives here as a
        // core-level subsystem rather than under rendering_engine.
        std::unique_ptr<core::audio> audio;
        std::unique_ptr<core::input> input;
        std::unique_ptr<platform::window> window;
        std::unique_ptr<rendering_engine::gpu::device> gpu;
        std::unique_ptr<rendering_engine::asset_cache> assets;
        std::unique_ptr<rendering_engine::renderer> renderer;
        // Rigid-body simulation, stepped once per fixed update. Outlives the
        // scenes so their physics components unregister against it.
        std::unique_ptr<runtime::physics::world> physics;
        // The Lua state scripted behaviours run in. Outlives the scenes, so
        // every scripted behaviour is gone before the state closes.
        std::unique_ptr<runtime::script_host> scripts;
        // Every scene: the persistent one plus whatever is loaded. New
        // content goes to scenes->active_scene().
        std::unique_ptr<runtime::scene_manager> scenes;

    private:
        bool m_quit_requested{false};
        // Owns the bus listener that sets m_quit_requested; dropped before the
        // bus is torn down so the [this] capture never outlives the engine.
        core::subscription m_quit_subscription;
        // Frames rendered so far, for diagnostics.frame_limit.
        unsigned int m_frames_rendered{0};
        // Debug builds: the line helper drawing the physics world's
        // colliders, alive while both the world and the renderer are up.
        std::unique_ptr<runtime::physics::debug_draw> m_physics_debug;
        // The tool layer drawn over every rendered frame, when the
        // executable installed one (see set_overlay).
        std::unique_ptr<runtime::overlay> m_overlay;
    };

    /**
     * @brief Returns the currently-live @ref engine instance.
     *
     * Logs a fatal error and throws @c std::logic_error if called when no
     * engine is constructed — in every build configuration, so a module
     * static that outlives the engine fails visibly rather than
     * dereferencing null. The pointer is installed by the @ref engine
     * constructor and cleared by its destructor.
     */
    engine& current_engine();
} // namespace runtime
