// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

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
