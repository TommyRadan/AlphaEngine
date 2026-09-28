// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
     * @ref tick once per fixed step (by @c core::time::fixed_delta_time, from
     * a behaviour's @c on_fixed_update, say), so a timer runs at the
     * simulation rate and pauses with it. Durations are seconds, the unit of
     * @c core::time.
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
         * @param duration_seconds Time until the timer fires (the period,
         *                         when repeating). A non-positive duration
         *                         fires on every tick (a repeating timer) or
         *                         on the first one (a one-shot).
         * @param repeating        Whether to rearm after firing.
         */
        explicit timer(double duration_seconds, bool repeating = false);

        /**
         * @brief Advances the timer by one step of @p delta_seconds.
         * @return How many times it fired during the step: 0 or 1 for a
         *         one-shot, and possibly more than 1 for a repeating timer
         *         whose period is shorter than the step.
         */
        uint32_t tick(double delta_seconds);

        /** @brief Rewinds to zero elapsed time and rearms a finished one-shot. */
        void reset();

        /** @brief True once a one-shot has fired; never true when repeating. */
        bool finished() const;

        bool repeating() const;
        double duration() const;

        /** @brief Time into the current period, in seconds. */
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
