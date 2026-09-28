// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file tween.hpp
 * @brief An eased interpolation between two values, advanced on the fixed
 *        update step and sampled smoothly at render time.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <core/math/curve.hpp>
#include <core/math/easing.hpp>

namespace core
{
    /** @brief What a @ref tween does when it reaches its end. */
    enum class tween_loop : uint8_t
    {
        // Stop at the end value and report finished.
        once,
        // Jump back to the start value and run again.
        loop,
        // Run back towards the start value, then forwards again, forever.
        ping_pong,
    };

    /**
     * @brief Moves a value from @c from to @c to over a duration, shaped by
     *        an @ref core::math::easing curve.
     *
     * Advanced once per fixed step with @ref tick (by the fixed step length
     * in seconds, @c core::time::fixed_delta_time — from a behaviour's
     * @c on_fixed_update, say), so it is deterministic and frame-rate
     * independent, and it stops while the game is paused. It keeps the elapsed time of
     * the previous step as well as the current one, which is what
     * @ref value(double) needs to hand the renderer a value between the two
     * — pass @c core::time::interpolation_alpha() and motion stays smooth
     * when the render rate runs ahead of the fixed rate. @c T is any type
     * @ref core::math::interpolate blends: @c float, the vectors, @c quat
     * (shortest-arc slerp) or a @c trs pose.
     */
    template<typename T>
    struct tween
    {
        tween() = default;

        tween(T from,
              T to,
              double duration_seconds,
              math::easing ease = math::easing::linear,
              tween_loop loop = tween_loop::once)
            : m_from{from}, m_to{to}, m_duration{duration_seconds}, m_ease{ease}, m_loop{loop}
        {
        }

        /** @brief Advances by one fixed step of @p delta_seconds (negative steps are ignored). */
        void tick(double delta_seconds)
        {
            m_previous_elapsed = m_elapsed;
            m_elapsed += std::max(delta_seconds, 0.0);
            if (m_loop == tween_loop::once)
            {
                m_elapsed = std::min(m_elapsed, std::max(m_duration, 0.0));
                return;
            }
            // Keep the pair in step with each other but bounded, so a tween
            // that runs for hours does not lose float precision: drop whole
            // cycles once both have passed one.
            const double cycle = m_loop == tween_loop::ping_pong ? 2.0 * m_duration : m_duration;
            if (cycle > 0.0 && m_previous_elapsed >= cycle)
            {
                const double whole = std::floor(m_previous_elapsed / cycle) * cycle;
                m_previous_elapsed -= whole;
                m_elapsed -= whole;
            }
        }

        /** @brief The value at the latest fixed step. */
        T value() const
        {
            return sample(m_elapsed);
        }

        /**
         * @brief The value @p alpha of the way from the previous fixed step
         *        to the latest one (0 gives the previous step's value, 1 the
         *        latest).
         */
        T value(double alpha) const
        {
            return sample(m_previous_elapsed + (m_elapsed - m_previous_elapsed) * std::clamp(alpha, 0.0, 1.0));
        }

        /** @brief True once a @ref tween_loop::once tween has reached its end. */
        bool finished() const
        {
            return m_loop == tween_loop::once && m_elapsed >= m_duration;
        }

        /** @brief Rewinds to the start value. */
        void reset()
        {
            m_elapsed = 0.0;
            m_previous_elapsed = 0.0;
        }

        /** @brief Seconds since the start (within the current cycle for a looping tween). */
        double elapsed() const
        {
            return m_elapsed;
        }

    private:
        T sample(double elapsed) const
        {
            if (!(m_duration > 0.0))
            {
                return m_to;
            }
            double progress = elapsed / m_duration;
            switch (m_loop)
            {
            case tween_loop::once:
                progress = std::min(progress, 1.0);
                break;
            case tween_loop::loop:
                progress = progress - std::floor(progress);
                break;
            case tween_loop::ping_pong:
            {
                const double phase = progress - 2.0 * std::floor(progress * 0.5);
                progress = phase <= 1.0 ? phase : 2.0 - phase;
                break;
            }
            }
            return math::interpolate(m_from, m_to, math::ease(m_ease, static_cast<float>(progress)));
        }

        T m_from{};
        T m_to{};
        double m_duration{0.0};
        math::easing m_ease{math::easing::linear};
        tween_loop m_loop{tween_loop::once};
        double m_elapsed{0.0};
        double m_previous_elapsed{0.0};
    };
} // namespace core
