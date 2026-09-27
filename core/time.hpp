// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file time.hpp
 * @brief Frame timing utilities (delta time, frame count, FPS).
 */

#pragma once

#include <cstdint>

namespace core
{
    /**
     * @brief Frame clock owned by @ref runtime::engine.
     *
     * Backed by the platform layer's high-resolution performance counter
     * (@c core::platform::performance_counter). Call
     * @ref perform_tick once per frame, at the top of the frame, so the
     * variable-rate accessors (@ref delta_time, @ref current_fps,
     * @ref frame_count) describe the frame being processed. Each instance
     * keeps its own previous-tick timestamp, so several clocks can coexist
     * (tests construct their own) without one instance's ticks feeding
     * another's delta.
     *
     * The very first tick reports a zero delta: there is no earlier frame to
     * measure from, and counting from construction would fold the whole engine
     * bring-up into frame one. From the second tick on, the delta is the
     * wall-clock time since the previous tick.
     *
     * The clock also drives a decoupled **fixed-step accumulator** so the
     * main loop can run deterministic, frame-rate-independent updates
     * separately from rendering. Each frame, feed the elapsed wall-clock
     * time into @ref accumulate, drain whole fixed steps with
     * @ref next_fixed_step (one game-logic update per step), then render
     * once. @ref interpolation_alpha exposes the fractional remainder for
     * blending between the last two simulated states: @c core::tween and
     * @c runtime::animator_component keep the previous fixed step's state
     * and sample between the two at render time. The other smooth path is
     * the per-render @c core::render_update event, which carries
     * @ref delta_time directly.
     *
     * Not thread-safe.
     */
    struct time
    {
        /**
         * @param fixed_delta_time   Length of one fixed update step, in
         *                           milliseconds (default 1/60 s). Must be
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
        explicit time(double fixed_delta_time = 1000.0 / 60.0, int max_steps_per_frame = 5);

        /**
         * @brief Advances the wall-clock by one frame, updating the delta
         *        time and incrementing the frame counter. Call exactly
         *        once per main-loop iteration, before anything reads
         *        @ref delta_time for that frame.
         */
        void perform_tick();

        /**
         * @brief Time between the last two ticks, in milliseconds.
         *
         * Zero before the second tick (see the class note).
         */
        double delta_time() const;

        /** @brief Milliseconds since the platform library was initialized. */
        float total_time() const;

        /**
         * @brief Number of ticks so far.
         *
         * With @ref perform_tick at the top of the frame this is the 1-based
         * index of the frame currently being processed: 1 during the first
         * frame, and 0 only before the main loop has started.
         */
        uint32_t frame_count() const;

        /**
         * @brief Instantaneous FPS derived from the most recent delta.
         * @return Frames per second, or 0 when the delta is below 1us.
         */
        float current_fps() const;

        /**
         * @brief Smoothed FPS for display.
         *
         * An exponential moving average of the frame time (the newest frame
         * weighted 0.1, roughly a ten-frame window), inverted — so one long
         * or short frame nudges the reading rather than making it jump the
         * way @ref current_fps does. Seeded by the first non-zero delta.
         * @return Frames per second, or 0 before any frame time is known.
         */
        float average_fps() const;

        /** @brief Length of one fixed update step, in milliseconds. */
        double fixed_delta_time() const;

        /**
         * @brief Adds elapsed real time to the fixed-step accumulator.
         *
         * Pass @ref delta_time once per frame, before draining steps. The
         * accumulator is clamped to @c max_steps_per_frame steps so a
         * single long frame (a stall, a debugger break) can never queue
         * more updates than that — without the clamp a slow frame would
         * schedule extra steps, which take even longer, spiralling the
         * simulation further behind every frame.
         *
         * @param frame_delta_time Wall-clock time elapsed this frame, in ms.
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
        uint64_t m_previous_ticks; // performance-counter reading at the last tick
        uint32_t m_frame_count;
        double m_delta_time;
        double m_average_delta_time; // exponential moving average of m_delta_time
        double m_fixed_delta_time;
        double m_accumulator;
        int m_max_steps_per_frame;
    };
} // namespace core
