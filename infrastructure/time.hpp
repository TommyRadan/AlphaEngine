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

namespace infrastructure
{
    /**
     * @brief Frame clock owned by @ref control::engine.
     *
     * Backed by SDL's high-resolution performance counter. Call
     * @ref perform_tick once per frame to advance the clock; the other
     * accessors report values from the most recent tick. Not thread-safe.
     */
    struct time
    {
        time();

        /**
         * @brief Advances the clock by one frame, updating the delta
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

    private:
        uint32_t m_frame_count;
        double m_delta_time;
    };
} // namespace infrastructure
