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

#include <infrastructure/time.hpp>
#include <SDL.h>

infrastructure::time::time() : m_frame_count{0}, m_delta_time{0} {}

void infrastructure::time::perform_tick()
{
    m_frame_count++;

    const uint64_t ticks = SDL_GetPerformanceCounter();

    static uint64_t frame_start_tick_count = ticks;
    m_delta_time = (double)((ticks - frame_start_tick_count) * 1000) / SDL_GetPerformanceFrequency();
    frame_start_tick_count = ticks;
}

const double infrastructure::time::delta_time() const
{
    return m_delta_time;
}

const float infrastructure::time::total_time() const
{
    return (float)SDL_GetTicks();
}

const uint32_t infrastructure::time::frame_count() const
{
    return m_frame_count;
}

const float infrastructure::time::current_fps() const
{
    if (m_delta_time < 0.001)
        return 0;
    return (float)(1000.0 / m_delta_time);
}
