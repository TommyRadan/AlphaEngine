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
 * @file timer.hpp
 * @brief A countdown driven by the fixed update step: fires once, or every
 *        period, as simulated time passes.
 */

#pragma once

#include <cstdint>

namespace core
{
    /**
     * @brief Counts simulated time towards a duration.
     *
     * Plain data with no clock of its own: the owner advances it with
     * @ref tick from its fixed-step handler (the @c core::frame event, whose
     * @c m_delta_time is the fixed step in milliseconds), so a timer runs at
     * the simulation rate and pauses with it. Durations are milliseconds,
     * the unit of @c core::time.
     *
     * A one-shot timer fires once, on the tick that reaches its duration,
     * and then reports @ref finished until @ref reset. A repeating timer
     * fires every period for as long as it is ticked, carrying the overshoot
     * into the next period so it does not drift.
     */
    struct timer
    {
        timer() = default;

        /**
         * @param duration_ms Time until the timer fires (the period, when
         *                    repeating). A non-positive duration fires on
         *                    every tick (a repeating timer) or on the first
         *                    one (a one-shot).
         * @param repeating   Whether to rearm after firing.
         */
        explicit timer(double duration_ms, bool repeating = false);

        /**
         * @brief Advances the timer by one step of @p delta_ms.
         * @return How many times it fired during the step: 0 or 1 for a
         *         one-shot, and possibly more than 1 for a repeating timer
         *         whose period is shorter than the step.
         */
        uint32_t tick(double delta_ms);

        /** @brief Rewinds to zero elapsed time and rearms a finished one-shot. */
        void reset();

        /** @brief True once a one-shot has fired; never true when repeating. */
        bool finished() const;

        bool repeating() const;
        double duration() const;

        /** @brief Time into the current period, in milliseconds. */
        double elapsed() const;

        /** @brief @ref elapsed over @ref duration, in [0, 1]. */
        float progress() const;

    private:
        double m_duration{0.0};
        double m_elapsed{0.0};
        bool m_repeating{false};
        bool m_finished{false};
    };
} // namespace core
