// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Auto exposure (auto_exposure_pass), stage 1: meters the HDR image
// tonemap is about to map into a small LUMINANCE_SIZE x LUMINANCE_SIZE
// target, the first level of the reduction to one average. Each output
// texel stands for a LUMINANCE_SIZE-th of the image in each axis and meters
// a 4 x 4 grid of bilinear taps spread over that footprint, so the
// reduction sees the whole frame rather than a sparse subsample.
//
// It writes r = the mean of weight * log2 luminance over its taps and
// g = the mean weight, which the reduction averages alike, so the final
// ratio r / g is the log2 geometric mean luminance of the metered taps:
// a mean of logs, which a few bright highlights cannot drag up the way
// they would an arithmetic one. A tap with next to no light (luminance
// below 2^-10: the cleared background where nothing was drawn, or a NaN /
// infinite texel) weighs 0, so an empty backdrop does not drag the
// exposure up to its limit; every other tap weighs 1.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

// The pass bakes its target size in; the fallback only keeps the file
// compiling on its own.
#ifndef LUMINANCE_SIZE
#define LUMINANCE_SIZE 64
#endif

const float MIN_LUMINANCE = 1.0 / 1024.0;

void main()
{
    vec2 footprint = vec2(1.0 / float(LUMINANCE_SIZE));
    float weightedLog = 0.0;
    float weight = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            vec2 offset = (vec2(float(x), float(y)) - 1.5) * 0.25 * footprint;
            vec3 color = textureLod(hdrColor, texCoord + offset, 0.0).rgb;
            float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
            if (isnan(luminance) || isinf(luminance) || luminance < MIN_LUMINANCE)
            {
                continue;
            }
            weightedLog += log2(luminance);
            weight += 1.0;
        }
    }
    fragColor = vec4(weightedLog / 16.0, weight / 16.0, 0.0, 1.0);
}
