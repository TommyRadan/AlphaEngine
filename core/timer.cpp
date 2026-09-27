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

#include <core/timer.hpp>

#include <algorithm>
#include <cmath>

namespace core
{
    timer::timer(double duration_ms, bool repeating) : m_duration{duration_ms}, m_repeating{repeating} {}

    uint32_t timer::tick(double delta_ms)
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

        m_elapsed += std::max(delta_ms, 0.0);
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
