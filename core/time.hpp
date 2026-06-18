/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
     * Backed by SDL's high-resolution performance counter. Call
     * @ref perform_tick once per frame to advance the wall-clock; the
     * variable-rate accessors (@ref delta_time, @ref current_fps) report
     * values from the most recent tick.
     *
     * The clock also drives a decoupled **fixed-step accumulator** so the
     * main loop can run deterministic, frame-rate-independent updates
     * separately from rendering. Each frame, feed the elapsed wall-clock
     * time into @ref accumulate, drain whole fixed steps with
     * @ref next_fixed_step (one game-logic update per step), then render
     * once using @ref interpolation_alpha to smooth between the last two
     * simulated states.
     *
     * Not thread-safe.
     */
    struct time
    {
        /**
         * @param fixed_delta_time   Length of one fixed update step, in
         *                           milliseconds (default 1/60 s).
         * @param max_steps_per_frame Upper bound on fixed steps drained in
         *                           a single frame. Caps the accumulator so
         *                           a long stall cannot queue an unbounded
         *                           backlog of updates — the
         *                           spiral-of-death guard.
         */
        explicit time(double fixed_delta_time = 1000.0 / 60.0, int max_steps_per_frame = 5);

        /**
         * @brief Advances the wall-clock by one frame, updating the delta
         *        time and incrementing the frame counter. Call exactly
         *        once per main-loop iteration.
         */
        void perform_tick();

        /** @brief Time between the last two ticks, in milliseconds. */
        const double delta_time() const;

        /** @brief Milliseconds since SDL was initialized. */
        const float total_time() const;

        /** @brief Number of ticks (frames) since startup. */
        const uint32_t frame_count() const;

        /**
         * @brief Instantaneous FPS derived from the most recent delta.
         * @return Frames per second, or 0 when the delta is below 1us.
         */
        const float current_fps() const;

        /** @brief Length of one fixed update step, in milliseconds. */
        const double fixed_delta_time() const;

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
         *         in [0, 1). Use to interpolate render-time state between
         *         the previous and current fixed update.
         */
        const double interpolation_alpha() const;

    private:
        uint32_t m_frame_count;
        double m_delta_time;
        double m_fixed_delta_time;
        double m_accumulator;
        int m_max_steps_per_frame;
    };
} // namespace core
