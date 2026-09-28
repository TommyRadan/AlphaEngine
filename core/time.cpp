// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/time.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    // Weight of the newest frame time in the average_fps moving average. 0.1
    // settles in roughly ten frames: quick enough to follow a real frame-rate
    // change, slow enough to hide single-frame jitter.
    constexpr double k_average_weight = 0.1;

    // Below one microsecond a delta carries no usable frame rate.
    constexpr double k_min_fps_delta = 1e-6;
} // namespace

core::time::time(double fixed_delta_time, int max_steps_per_frame)
    : m_start{clock::now()}, m_previous_tick{m_start}, m_frame_count{0}, m_delta_time{0}, m_unscaled_delta_time{0},
      m_total_time{0}, m_average_delta_time{0}, m_fixed_delta_time{fixed_delta_time}, m_accumulator{0},
      m_time_scale{1.0}, m_resume_time_scale{1.0}, m_max_steps_per_frame{max_steps_per_frame}
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
    const clock::time_point now = clock::now();

    if (m_frame_count == 0)
    {
        // First frame: nothing precedes it, and measuring from construction
        // would report the whole engine bring-up as one enormous frame.
        m_unscaled_delta_time = 0.0;
    }
    else
    {
        m_unscaled_delta_time = std::chrono::duration<double>(now - m_previous_tick).count();
        // Seed the average with the first real frame time rather than ramping
        // up from zero, then blend each new sample in.
        m_average_delta_time =
            m_average_delta_time > 0.0
                ? m_average_delta_time + k_average_weight * (m_unscaled_delta_time - m_average_delta_time)
                : m_unscaled_delta_time;
    }
    m_delta_time = m_unscaled_delta_time * m_time_scale;
    m_total_time = std::chrono::duration<double>(now - m_start).count();

    m_previous_tick = now;
    ++m_frame_count;
}

double core::time::delta_time() const
{
    return m_delta_time;
}

double core::time::unscaled_delta_time() const
{
    return m_unscaled_delta_time;
}

double core::time::total_time() const
{
    return m_total_time;
}

uint32_t core::time::frame_count() const
{
    return m_frame_count;
}

float core::time::current_fps() const
{
    if (m_unscaled_delta_time < k_min_fps_delta)
    {
        return 0.0f;
    }
    return static_cast<float>(1.0 / m_unscaled_delta_time);
}

float core::time::average_fps() const
{
    if (m_average_delta_time < k_min_fps_delta)
    {
        return 0.0f;
    }
    return static_cast<float>(1.0 / m_average_delta_time);
}

double core::time::fixed_delta_time() const
{
    return m_fixed_delta_time;
}

double core::time::time_scale() const
{
    return m_time_scale;
}

void core::time::set_time_scale(double scale)
{
    if (std::isnan(scale))
    {
        return;
    }
    m_time_scale = std::clamp(scale, 0.0, max_time_scale);
    if (m_time_scale > 0.0)
    {
        m_resume_time_scale = m_time_scale;
    }
}

bool core::time::is_paused() const
{
    return m_time_scale <= 0.0;
}

void core::time::set_paused(bool paused)
{
    if (paused)
    {
        m_time_scale = 0.0;
    }
    else if (m_time_scale <= 0.0)
    {
        m_time_scale = m_resume_time_scale;
    }
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
