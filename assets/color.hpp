// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

namespace assets
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

    // The colour space an image was authored in. sRGB content (albedo, base
    // colour, emissive, sprites) is decoded to linear when it is sampled;
    // linear data (normals, metalness, roughness, AO) is sampled as stored.
    // The renderer picks the matching texel format from it, and the KTX2
    // decoder picks its transcode target by it.
    enum class color_space
    {
        linear,
        srgb,
    };
} // namespace assets
