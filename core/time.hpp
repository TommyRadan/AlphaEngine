// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file time.hpp
 * @brief Frame timing utilities (delta time in seconds, time scale, fixed steps, FPS).
 */

#pragma once

#include <chrono>
#include <cstdint>

namespace core
{
    /**
     * @brief Frame clock owned by @ref runtime::engine.
     *
     * Backed by @c std::chrono::steady_clock. Every duration it reports is in
     * seconds, as a @c double. Call @ref perform_tick once per frame, at the
     * top of the frame, so the variable-rate accessors (@ref delta_time,
     * @ref unscaled_delta_time, @ref total_time, @ref current_fps,
     * @ref frame_count) describe the frame being processed. Each instance
     * keeps its own previous-tick timestamp, so several clocks can coexist
     * without one instance's ticks feeding another's delta.
     *
     * The very first tick reports a zero delta: there is no earlier frame to
     * measure from, and counting from construction would fold the whole engine
     * bring-up into frame one. From the second tick on, the delta is the
     * wall-clock time since the previous tick.
     *
     * **Time scale.** @ref delta_time is the game's delta: the real time
     * scaled by @ref time_scale, which is 1 by default, slows the game down
     * below 1, speeds it up above 1 and pauses it at 0.
     * @ref unscaled_delta_time is the real time whatever the scale, for UI and
     * tools that keep running while the game is paused. A new scale applies
     * from the next tick.
     *
     * The clock also drives a decoupled **fixed-step accumulator** so the
     * engine can run deterministic, frame-rate-independent updates
     * separately from rendering. Each frame the scheduler
     * (@c runtime::scheduler) feeds the scaled delta into @ref accumulate,
     * drains whole fixed steps with @ref next_fixed_step (one simulation step
     * each), then renders once; a time scale of 0 therefore runs no steps.
     * @ref interpolation_alpha exposes the fractional remainder for blending
     * between the last two simulated states: @c core::tween,
     * @c runtime::animator_component and the physics world keep the previous
     * fixed step's state and sample between the two at render time.
     *
     * Not thread-safe.
     */
    struct time
    {
        /** @brief Largest time scale @ref set_time_scale accepts. */
        static constexpr double max_time_scale = 100.0;

        /**
         * @param fixed_delta_time   Length of one fixed update step, in
         *                           seconds (default 1/60 s). Must be
         *                           greater than zero.
         * @param max_steps_per_frame Upper bound on fixed steps drained in
         *                           a single frame. Caps the accumulator so
         *                           a long stall cannot queue an unbounded
         *                           backlog of updates — the
         *                           spiral-of-death guard. Must be at least 1.
         * @throws std::invalid_argument when either bound is violated: a zero
         *         step would make @ref next_fixed_step loop forever and
         *         @ref interpolation_alpha divide by zero, and a cap below one
         *         would clamp the accumulator to nothing (or negative).
         */
        explicit time(double fixed_delta_time = 1.0 / 60.0, int max_steps_per_frame = 5);

        /**
         * @brief Advances the wall-clock by one frame, updating the deltas
         *        and the total time and incrementing the frame counter. Call
         *        exactly once per main-loop iteration, before anything reads
         *        @ref delta_time for that frame.
         */
        void perform_tick();

        /**
         * @brief Game time between the last two ticks, in seconds: the real
         *        time scaled by the time scale in effect at the latest tick.
         *
         * Zero before the second tick (see the class note) and while paused.
         */
        double delta_time() const;

        /**
         * @brief Real time between the last two ticks, in seconds, whatever
         *        the time scale. Zero before the second tick.
         */
        double unscaled_delta_time() const;

        /** @brief Real seconds from this clock's construction to the latest tick. */
        double total_time() const;

        /**
         * @brief Number of ticks so far.
         *
         * With @ref perform_tick at the top of the frame this is the 1-based
         * index of the frame currently being processed: 1 during the first
         * frame, and 0 only before the main loop has started.
         */
        uint32_t frame_count() const;

        /**
         * @brief Instantaneous FPS derived from the most recent real delta.
         * @return Frames per second, or 0 when the delta is below 1us.
         */
        float current_fps() const;

        /**
         * @brief Smoothed FPS for display.
         *
         * An exponential moving average of the real frame time (the newest
         * frame weighted 0.1, roughly a ten-frame window), inverted — so one
         * long or short frame nudges the reading rather than making it jump
         * the way @ref current_fps does. Seeded by the first non-zero delta.
         * @return Frames per second, or 0 before any frame time is known.
         */
        float average_fps() const;

        /** @brief Length of one fixed update step, in seconds. */
        double fixed_delta_time() const;

        /** @brief The game time scale: 1 is real time, 0 is paused. */
        double time_scale() const;

        /**
         * @brief Sets the game time scale, clamped to [0, @ref max_time_scale]
         *        (a NaN is ignored); applies from the next tick.
         *
         * A positive scale is also the one @ref set_paused(false) resumes to.
         */
        void set_time_scale(double scale);

        /** @brief True while the time scale is 0. */
        bool is_paused() const;

        /**
         * @brief Pauses the game (time scale 0) or resumes it at the last
         *        positive scale it ran at (1 if it never ran at another);
         *        applies from the next tick. Pausing a paused clock and
         *        resuming a running one change nothing.
         */
        void set_paused(bool paused);

        /**
         * @brief Adds elapsed game time to the fixed-step accumulator.
         *
         * Pass @ref delta_time once per frame, before draining steps. The
         * accumulator is clamped to @c max_steps_per_frame steps so a
         * single long frame (a stall, a debugger break) can never queue
         * more updates than that — without the clamp a slow frame would
         * schedule extra steps, which take even longer, spiralling the
         * simulation further behind every frame.
         *
         * @param frame_delta_time Game time elapsed this frame, in seconds.
         */
        void accumulate(double frame_delta_time);

        /**
         * @brief Drains one fixed step from the accumulator.
         * @return true if a full @ref fixed_delta_time was available (and
         *         consumed), false once the remainder is below one step.
         *         Drive the fixed-update loop with
         *         @c while (time.next_fixed_step()) { update(); }.
         */
        bool next_fixed_step();

        /**
         * @brief Fractional progress toward the next fixed step.
         * @return Accumulator remainder divided by @ref fixed_delta_time,
         *         in [0, 1). For interpolating render-time state between the
         *         previous and current fixed update (see the class note).
         */
        double interpolation_alpha() const;

    private:
        using clock = std::chrono::steady_clock;

        clock::time_point m_start;         // construction, the origin of total_time
        clock::time_point m_previous_tick; // the clock reading at the last tick
        uint32_t m_frame_count;
        double m_delta_time;          // scaled
        double m_unscaled_delta_time; // real
        double m_total_time;
        double m_average_delta_time; // exponential moving average of m_unscaled_delta_time
        double m_fixed_delta_time;
        double m_accumulator;
        double m_time_scale;
        double m_resume_time_scale; // the scale set_paused(false) restores
        int m_max_steps_per_frame;
    };
} // namespace core
