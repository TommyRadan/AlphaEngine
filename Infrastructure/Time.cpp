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

#include <Infrastructure/Time.hpp>
#include <SDL.h>

Infrastructure::Time* const Infrastructure::Time::GetInstance()
{
    static Time* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Time();
    }

    return instance;
}

Infrastructure::Time::Time() :
    m_FrameCount { 0 },
    m_DeltaTime { 0 }
{}

void Infrastructure::Time::PerformTick()
{
    m_FrameCount++;

    const uint64_t ticks = SDL_GetPerformanceCounter();

    static uint64_t frameStartTickCount = ticks;
    m_DeltaTime = (double)((ticks - frameStartTickCount)*1000) / SDL_GetPerformanceFrequency();
    frameStartTickCount = ticks;
}

const double Infrastructure::Time::DeltaTime() const
{
    return m_DeltaTime;
}

const float Infrastructure::Time::TotalTime() const
{
    return (float) SDL_GetTicks();
}

const uint32_t Infrastructure::Time::FrameCount() const
{
    return m_FrameCount;
}

const float Infrastructure::Time::CurrentFps() const
{
    if (m_DeltaTime < 0.001) return 0;
    return (float)(1000.0/m_DeltaTime);
}
