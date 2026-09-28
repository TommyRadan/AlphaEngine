// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/timer.hpp>

#include <algorithm>
#include <cmath>

namespace core
{
    timer::timer(double duration_seconds, bool repeating) : m_duration{duration_seconds}, m_repeating{repeating} {}

    uint32_t timer::tick(double delta_seconds)
    {
        if (m_finished)
        {
            return 0;
        }
        if (!(m_duration > 0.0))
        {
            // Nothing to count: fire now (every tick, when repeating).
            m_finished = !m_repeating;
            return 1;
        }

        m_elapsed += std::max(delta_seconds, 0.0);
        if (m_elapsed < m_duration)
        {
            return 0;
        }
        if (!m_repeating)
        {
            m_elapsed = m_duration;
            m_finished = true;
            return 1;
        }

        // Whole periods that fit in the elapsed time each fire once; the
        // remainder starts the next period, so the timer keeps its phase.
        const double periods = std::floor(m_elapsed / m_duration);
        m_elapsed -= periods * m_duration;
        return static_cast<uint32_t>(periods);
    }

    void timer::reset()
    {
        m_elapsed = 0.0;
        m_finished = false;
    }

    bool timer::finished() const
    {
        return m_finished;
    }

    bool timer::repeating() const
    {
        return m_repeating;
    }

    double timer::duration() const
    {
        return m_duration;
    }

    double timer::elapsed() const
    {
        return m_elapsed;
    }

    float timer::progress() const
    {
        if (!(m_duration > 0.0))
        {
            return m_finished ? 1.0f : 0.0f;
        }
        return static_cast<float>(std::clamp(m_elapsed / m_duration, 0.0, 1.0));
    }
} // namespace core
