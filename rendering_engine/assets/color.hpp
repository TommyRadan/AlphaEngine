// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

namespace rendering_engine
{
    struct color
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    // Pixel buffers are handed to the GPU as tightly packed RGBA8 texels
    // (width * height * sizeof(color) bytes), so the struct must be exactly
    // four bytes with no padding.
    static_assert(sizeof(color) == 4, "color must be a packed 4-byte RGBA8 texel");
} // namespace rendering_engine
