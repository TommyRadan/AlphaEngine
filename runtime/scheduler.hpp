// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file scheduler.hpp
 * @brief The frame's explicit schedule: named stages in a fixed order, and
 *        the engine and game systems registered into them.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace core
{
    struct time;
}

namespace runtime
{
    /**
     * @brief The stages of a frame, in the order @ref scheduler runs them.
     *
     * @ref stage::scripts_fixed, @ref stage::physics and
     * @ref stage::post_physics make up the fixed stage: the three run once per
     * fixed step the clock drains this frame, in that order within the step,
     * so a frame runs them zero, one or several times. Every other stage runs
     * once per frame. The renderer draws the frame after
     * @ref stage::render_extract.
     */
    enum class stage : std::uint8_t
    {
        input,         ///< OS and window events, input state, buffered events, finished asset loads.
        scripts_fixed, ///< Fixed step: game logic (@c on_fixed_update, in hierarchy order).
        physics,       ///< Fixed step: the rigid-body simulation.
        post_physics,  ///< Fixed step: what follows the simulation, then the deferred commands.
        update,        ///< Render-rate game logic (@c on_update, in hierarchy order), then the deferred commands.
        animation,     ///< Animated poses, sampled between the last two fixed steps.
        transform_propagation, ///< Every world matrix settled.
        audio,                 ///< Listener and source poses handed to the mixer, the mixer fed.
        render_extract,        ///< The tool overlay built, the render proxies and debug helpers written.
    };

    /** @brief Number of @ref stage values. */
    inline constexpr std::size_t stage_count = 9;

    /** @brief The stage's name as written in @ref stage, e.g. "scripts_fixed". */
    const char* stage_name(stage value) noexcept;

    /** @brief True for the three stages that run once per fixed step. */
    bool is_fixed_stage(stage value) noexcept;

    /**
     * @brief The clock as a system sees it, in seconds.
     *
     * A change to the time scale made during a frame applies from the next
     * frame, so every system of one frame sees the same scale.
     */
    struct frame_time
    {
        /**
         * @brief Game time to advance by: the fixed step length in a fixed
         *        stage, the frame's scaled delta (the real delta times the
         *        time scale; zero while paused) in every other stage.
         */
        double delta{0.0};

        /** @brief Real time since the previous frame, whatever the time scale: for UI and tools. */
        double unscaled_delta{0.0};

        /** @brief The time scale this frame runs at; 0 while paused. */
        double time_scale{1.0};

        /**
         * @brief How far the clock stands between the last two fixed steps,
         *        in [0, 1) (@c core::time::interpolation_alpha): what the
         *        stages after the fixed stage sample their interpolated
         *        state at.
         */
        double alpha{0.0};
    };

    /** @brief Where a system runs within its stage, and whether it runs while the game is paused. */
    struct system_options
    {
        /**
         * @brief Lower runs first within the stage; systems of equal order
         *        run in the order they were added. The engine's own systems
         *        use the orders @ref engine_order lists, so a game system
         *        places itself before or after them.
         */
        int order{0};

        /**
         * @brief Whether the system runs while the time scale is 0. Game
         *        logic does not (the default); what keeps the frame, its
         *        input, the tools and the UI alive does.
         */
        bool while_paused{false};
    };

    /**
     * @brief The orders the engine's own systems run at within their stages
     *        (@ref system_options::order), for a game system that runs before
     *        or after one of them: a lower order runs first, and a system of
     *        an equal order added later runs after the engine's.
     */
    namespace engine_order
    {
        // stage::input
        inline constexpr int window_events = -400; ///< The window pumps OS events onto the event bus.
        inline constexpr int input_frame = -300;   ///< @c core::input latches the frame's cursor motion.
        inline constexpr int event_queue = -200;   ///< The events buffered since the last frame are delivered.
        inline constexpr int asset_uploads = -100; ///< Finished asynchronous asset loads are uploaded.

        // stage::scripts_fixed
        inline constexpr int input_step = -100; ///< @c core::input latches the step's pressed / released edges.
        inline constexpr int fixed_update = 0;  ///< Every scene's @c on_fixed_update, in hierarchy order.

        // stage::physics
        inline constexpr int physics_step = 0; ///< The rigid-body world steps.

        // stage::post_physics
        inline constexpr int animation_step = 0; ///< Every animator advances by the fixed step.

        // stage::post_physics and stage::update
        inline constexpr int deferred_commands = 1000; ///< Every scene applies its deferred commands, last.

        // stage::update
        inline constexpr int physics_interpolation = -300; ///< Simulated nodes placed between the last two steps.
        inline constexpr int script_reload = -200;         ///< Debug builds: changed scripts are reloaded.
        inline constexpr int gltf_spawn = -100;            ///< Pending @c instantiate_gltf_when_ready spawns.
        inline constexpr int scene_update = 0;             ///< Every scene's @c on_update, in hierarchy order.

        // stage::animation
        inline constexpr int animation = 0; ///< Every animator writes its pose, between the last two steps.

        // stage::transform_propagation
        inline constexpr int transform_propagation = 0; ///< Every scene settles its world matrices.

        // stage::audio
        inline constexpr int audio_poses = -100; ///< Listener and source poses are handed to the mixer.
        inline constexpr int audio_mix = 0;      ///< The mixer tops the device up, at the time scale.

        // stage::render_extract
        inline constexpr int overlay = -100;      ///< The tool overlay (the editor) builds its frame.
        inline constexpr int render_proxies = 0;  ///< Meshes, UI, lights and cameras are extracted.
        inline constexpr int debug_helpers = 100; ///< The debug helpers rebuild what they follow.
    } // namespace engine_order

    /** @brief What one system cost in the latest complete frame. */
    struct system_timing
    {
        std::string name;
        stage where{stage::input};
        /** @brief CPU time across every run of the frame, in milliseconds. */
        double milliseconds{0.0};
        /** @brief How many times it ran (once per fixed step in a fixed stage; 0 when skipped). */
        std::uint32_t runs{0};
    };

    /** @brief What one stage cost in the latest complete frame. */
    struct stage_timing
    {
        /** @brief CPU time across every run of the frame, in milliseconds, its systems included. */
        double milliseconds{0.0};
        /** @brief How many times it ran (the frame's fixed steps, for a fixed stage). */
        std::uint32_t runs{0};
    };

    struct scheduler;

    /**
     * @brief Owning handle for one system added through @ref scheduler::add.
     *
     * Destroying or @ref reset -ing it removes the system; a system removed
     * while its stage runs (even by itself) finishes that run and is not run
     * again. Movable, not copyable; a default-constructed handle is empty.
     * The handle reaches its scheduler through a weak reference, so one that
     * outlives the scheduler does nothing.
     */
    struct [[nodiscard]] system_registration
    {
        system_registration() noexcept = default;
        ~system_registration();

        system_registration(const system_registration&) = delete;
        system_registration& operator=(const system_registration&) = delete;
        system_registration(system_registration&& other) noexcept;
        system_registration& operator=(system_registration&& other) noexcept;

        /** @brief Removes the system now and empties the handle. */
        void reset();

        /** @brief True while the handle owns a system. */
        explicit operator bool() const noexcept;

    private:
        friend struct scheduler;

        system_registration(std::weak_ptr<scheduler*> owner, std::uint64_t id) noexcept;

        std::weak_ptr<scheduler*> m_owner;
        std::uint64_t m_id{0};
    };

    /**
     * @brief Runs the frame: every system of every @ref stage, in stage
     *        order, the fixed stage once per fixed step.
     *
     * Owned by @ref runtime::engine as @c engine::systems. A system is a
     * named function of the @ref frame_time, added to one stage with
     * @ref add; the engine adds its own (the window and input, the fixed
     * update, the physics step, the deferred commands, the scene update,
     * animation, transform propagation, audio and the render extraction) and
     * game code adds more the same way, so the order the frame runs in is
     * this schedule, never the order anything subscribed to an event in.
     *
     * **A frame** is @ref run_frame, then, when there is a frame to draw,
     * @ref run_render_extract and the renderer. @ref run_frame runs
     * @ref stage::input; adds the frame's scaled delta to the clock's
     * fixed-step accumulator and, for every fixed step it drains, runs
     * @ref stage::scripts_fixed, @ref stage::physics and
     * @ref stage::post_physics; then @ref stage::update,
     * @ref stage::animation, @ref stage::transform_propagation and
     * @ref stage::audio.
     *
     * **Time scale and pause.** The clock's time scale scales the delta the
     * accumulator receives, so it slows the fixed steps down, speeds them up
     * or, at 0, stops them. While it is 0 the scheduler also skips every
     * system not added with @ref system_options::while_paused.
     *
     * **Profiling.** Every system and every stage run is timed with
     * @c std::chrono::steady_clock; @ref stage_timings and
     * @ref system_timings report the latest complete frame.
     *
     * Systems may add and remove systems while a stage runs: a removal takes
     * effect at once (the system is not run again), an addition when the
     * stage run in progress returns. Main-thread-only.
     */
    struct scheduler
    {
        /** @brief A system: called once per run of its stage. */
        using system_function = std::function<void(const frame_time&)>;

        /** @brief A scheduler that drives its fixed steps and reads its time from @p clock. */
        explicit scheduler(core::time& clock);
        ~scheduler();

        scheduler(const scheduler&) = delete;
        scheduler& operator=(const scheduler&) = delete;
        scheduler(scheduler&&) = delete;
        scheduler& operator=(scheduler&&) = delete;

        /**
         * @brief Adds @p run to @p where as @p name (the name the profiler
         *        shows); it runs until the returned handle goes.
         *
         * @p run must not throw. Returns an empty handle for an empty
         * function.
         */
        system_registration add(stage where, std::string name, system_function run, system_options options = {});

        /**
         * @brief Runs the simulation half of a frame (see the class notes):
         *        from @ref stage::input through @ref stage::audio.
         *
         * Call once per frame, after the clock has ticked.
         */
        void run_frame();

        /** @brief Runs @ref stage::render_extract for the frame @ref run_frame ran; call it only when drawing one. */
        void run_render_extract();

        /** @brief Fixed steps the latest @ref run_frame ran. */
        std::uint32_t fixed_steps() const noexcept;

        /** @brief Per stage, in @ref stage order: the cost of the latest complete frame. */
        const std::array<stage_timing, stage_count>& stage_timings() const noexcept;

        /** @brief Every system, in stage order and run order within a stage: its cost in the latest complete frame. */
        const std::vector<system_timing>& system_timings() const noexcept;

    private:
        friend struct system_registration;

        struct entry;

        void remove(std::uint64_t id);

        // Puts @p created into its stage's list, after every system of the
        // same or a lower order.
        void insert(std::unique_ptr<entry> created);

        // Runs every system of @p where once with @p time and times it.
        void run_stage(stage where, const frame_time& time);

        // The frame_time of the frame in progress, with @p delta as its delta.
        frame_time make_time(double delta) const;

        // Applies the additions and removals made while a stage ran.
        void apply_changes();

        // Moves the frame's timings into the published ones and resets them.
        void publish_timings();

        core::time* m_clock;
        // The handle's weak reference to this scheduler.
        std::shared_ptr<scheduler*> m_self;

        // Per stage, the systems in run order.
        std::array<std::vector<std::unique_ptr<entry>>, stage_count> m_stages;
        // Systems added while a stage ran, waiting to be merged in.
        std::vector<std::unique_ptr<entry>> m_added;
        std::uint64_t m_next_id{1};
        // Depth of stage runs (and change applications) in progress;
        // additions and removals wait while it is non-zero.
        int m_running{0};
        bool m_removed_pending{false};

        // The time scale the frame in progress runs at (read once, at its start).
        double m_frame_scale{1.0};
        std::uint32_t m_fixed_steps{0};

        std::array<stage_timing, stage_count> m_frame_stages{};
        std::array<stage_timing, stage_count> m_stage_timings{};
        std::vector<system_timing> m_system_timings;
        // Set when a system came or went, so the published list is rebuilt.
        bool m_timings_stale{true};
    };
} // namespace runtime
