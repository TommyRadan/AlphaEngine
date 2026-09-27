// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Auto exposure (auto_exposure_pass), stage 3: eye adaptation, drawn into
// a 1 x 1 target. The last reduction level is 4 x 4, so four bilinear taps
// at the quarter points average it (and so the whole frame); the ratio of
// its channels is the log2 geometric-mean luminance of the metered pixels
// (see exposure_luminance.frag.glsl). That becomes a target brightness in
// EV100 (log2(L * S / K) with the usual S = 100, K = 12.5, i.e.
// log2 L + 3), clamped to the configured [min, max] range, and the adapted
// brightness eases toward it from last frame's value with an exponential
// approach at the rate for its direction: speed.x while the scene
// brightens, speed.y while it darkens. A reset (the first metered frame,
// or the first after re-enabling) snaps to the target instead, so nothing
// fades in from a stale or undefined value. A frame with nothing metered
// (every pixel black) keeps last frame's value, or on a reset starts from
// the brightness at which the exposure is exactly 1.
//
// The output texel carries r = the adapted EV100 (next frame's history,
// copied out by the store stage) and g = log2 of the exposure tonemap
// applies: the scale that maps the adapted average luminance to middle
// grey (0.18), raised by the compensation in stops. Both are logs, so the
// rgba16f target keeps them precise over the whole EV range.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D luminance;
layout(set = 0, binding = 1) uniform sampler2D history;

// std140, 32 bytes, mirroring auto_exposure_pass's params block:
//     0   vec4 range  x min EV100, y max EV100, z compensation in stops,
//                     w 1 to snap to the target (reset), else 0
//    16   vec4 speed  x rate while brightening, y rate while darkening
//                     (per second), z this frame's delta in seconds
layout(set = 0, binding = 2, std140) uniform Adaptation
{
    vec4 range;
    vec4 speed;
} u_adapt;

const float MIDDLE_GREY = 0.18;

// EV100 of an average luminance L is log2(L) + 3; this is the brightness
// whose middle-grey exposure is exactly 1.
const float NEUTRAL_EV = 3.0 + log2(MIDDLE_GREY);

void main()
{
    vec2 metered = 0.25 * (textureLod(luminance, vec2(0.25, 0.25), 0.0).rg +
                           textureLod(luminance, vec2(0.75, 0.25), 0.0).rg +
                           textureLod(luminance, vec2(0.25, 0.75), 0.0).rg +
                           textureLod(luminance, vec2(0.75, 0.75), 0.0).rg);

    float previous = texelFetch(history, ivec2(0, 0), 0).r;
    bool reset = u_adapt.range.w > 0.5 || isnan(previous) || isinf(previous);

    float target;
    if (metered.g > 1.0e-6)
    {
        target = clamp(metered.r / metered.g + 3.0, u_adapt.range.x, u_adapt.range.y);
    }
    else
    {
        target = reset ? clamp(NEUTRAL_EV, u_adapt.range.x, u_adapt.range.y) : previous;
    }

    float adapted = target;
    if (!reset)
    {
        float rate = target > previous ? u_adapt.speed.x : u_adapt.speed.y;
        adapted = previous + (target - previous) * (1.0 - exp(-u_adapt.speed.z * rate));
    }

    // The average luminance of the adapted brightness is 2^(adapted - 3).
    float logExposure = log2(MIDDLE_GREY) - (adapted - 3.0) + u_adapt.range.z;
    fragColor = vec4(adapted, logExposure, 0.0, 1.0);
}
