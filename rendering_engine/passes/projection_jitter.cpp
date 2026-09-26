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

#include <rendering_engine/passes/projection_jitter.hpp>

namespace rendering_engine
{
    float halton(uint32_t index, uint32_t base)
    {
        float result = 0.0f;
        float fraction = 1.0f;
        while (index > 0)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    core::math::vec2 taa_jitter_ndc(uint64_t frame_index, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return core::math::vec2{0.0f, 0.0f};
        }
        // Halton is 1-based; index 0 would sit exactly on the pixel centre
        // and waste a sequence slot.
        const auto sample = static_cast<uint32_t>(frame_index % taa_jitter_period) + 1u;
        // (halton - 0.5) spans (-0.5, 0.5) pixels; a pixel is 2/width NDC.
        const float x = (halton(sample, 2u) - 0.5f) * 2.0f / static_cast<float>(width);
        const float y = (halton(sample, 3u) - 0.5f) * 2.0f / static_cast<float>(height);
        return core::math::vec2{x, y};
    }

    core::math::mat4 jitter_projection(const core::math::mat4& projection, const core::math::vec2& jitter_ndc)
    {
        // T(jitter) * projection with T translating clip x / y by
        // jitter * w: column-major storage puts row r of column c at
        // m[c * 4 + r], so rows 0 and 1 each gain jitter times row 3.
        core::math::mat4 jittered = projection;
        for (int column = 0; column < 4; ++column)
        {
            const float w_row = projection.m[column * 4 + 3];
            jittered.m[column * 4 + 0] += jitter_ndc.x * w_row;
            jittered.m[column * 4 + 1] += jitter_ndc.y * w_row;
        }
        return jittered;
    }
} // namespace rendering_engine
