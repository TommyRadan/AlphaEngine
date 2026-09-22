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

#include <core/time.hpp>

#include <stdexcept>

#include <SDL3/SDL.h>

namespace
{
    // Weight of the newest frame time in the average_fps moving average. 0.1
    // settles in roughly ten frames: quick enough to follow a real frame-rate
    // change, slow enough to hide single-frame jitter.
    constexpr double k_average_weight = 0.1;
} // namespace

core::time::time(double fixed_delta_time, int max_steps_per_frame)
    : m_previous_ticks{SDL_GetPerformanceCounter()}, m_frame_count{0}, m_delta_time{0}, m_average_delta_time{0},
      m_fixed_delta_time{fixed_delta_time}, m_accumulator{0}, m_max_steps_per_frame{max_steps_per_frame}
{
    // Written as !(x > 0) so a NaN step is rejected along with zero and negatives.
    if (!(fixed_delta_time > 0.0))
    {
        throw std::invalid_argument{"time: fixed_delta_time must be greater than zero"};
    }
    if (max_steps_per_frame < 1)
    {
        throw std::invalid_argument{"time: max_steps_per_frame must be at least 1"};
    }
}

void core::time::perform_tick()
{
    const uint64_t ticks = SDL_GetPerformanceCounter();

    if (m_frame_count == 0)
    {
        // First frame: nothing precedes it, and measuring from construction
        // would report the whole engine bring-up as one enormous frame.
        m_delta_time = 0.0;
    }
    else
    {
        m_delta_time =
            static_cast<double>(ticks - m_previous_ticks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
        // Seed the average with the first real frame time rather than ramping
        // up from zero, then blend each new sample in.
        m_average_delta_time = m_average_delta_time > 0.0
                                   ? m_average_delta_time + k_average_weight * (m_delta_time - m_average_delta_time)
                                   : m_delta_time;
    }

    m_previous_ticks = ticks;
    ++m_frame_count;
}

double core::time::delta_time() const
{
    return m_delta_time;
}

float core::time::total_time() const
{
    return (float)SDL_GetTicks();
}

uint32_t core::time::frame_count() const
{
    return m_frame_count;
}

float core::time::current_fps() const
{
    if (m_delta_time < 0.001)
        return 0;
    return (float)(1000.0 / m_delta_time);
}

float core::time::average_fps() const
{
    if (m_average_delta_time < 0.001)
        return 0;
    return (float)(1000.0 / m_average_delta_time);
}

double core::time::fixed_delta_time() const
{
    return m_fixed_delta_time;
}

void core::time::accumulate(double frame_delta_time)
{
    m_accumulator += frame_delta_time;

    // Spiral-of-death guard: never let the accumulator hold more than
    // m_max_steps_per_frame whole steps. Anything beyond is dropped, so
    // the simulation runs slower than real time during a stall rather
    // than trying (and failing) to catch up.
    const double max_accumulated = m_fixed_delta_time * m_max_steps_per_frame;
    if (m_accumulator > max_accumulated)
    {
        m_accumulator = max_accumulated;
    }
}

bool core::time::next_fixed_step()
{
    if (m_accumulator < m_fixed_delta_time)
    {
        return false;
    }
    m_accumulator -= m_fixed_delta_time;
    return true;
}

double core::time::interpolation_alpha() const
{
    return m_accumulator / m_fixed_delta_time;
}
