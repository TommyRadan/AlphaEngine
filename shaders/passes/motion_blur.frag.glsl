// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Camera motion blur (motion_blur_pass), drawn over the shared fullscreen
// triangle into a full-resolution rgba16f target the rest of the HDR post
// chain (bloom, auto exposure, tonemap) then reads instead of the scene
// colour. Each pixel averages the scene colour along its own motion vector
// from velocity_pass: the UV displacement since the previous frame,
// unjittered, so a static camera blurs nothing whatever the temporal-AA
// jitter. The vector is scaled by the shutter (the fraction of one frame's
// motion the exposure spans), measured in pixels, clamped to the maximum
// radius and sampled symmetrically about the pixel, so the smear stays
// centred on where the surface is this frame. The taps start at a per-pixel
// interleaved-gradient-noise offset (moved per frame while temporal AA is
// there to average it), turning a low tap count's banding into fine noise.
// A pixel whose motion is under half a pixel keeps its colour unblurred.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D velocity;

// std140, 32 bytes, mirroring motion_blur_pass's params block:
//     0   vec4 blur   x shutter scale, y max radius in pixels, z tap count,
//                     w noise frame offset
//    16   vec4 frame  xy target size in pixels, zw 1 / size
layout(set = 0, binding = 2, std140) uniform MotionBlur
{
    vec4 blur;
    vec4 frame;
} u_blur;

// Upper bound of the tap count; the pass clamps its setting to this too.
const int MAX_TAPS = 32;

float interleaved_gradient_noise(vec2 pixel, float frame)
{
    pixel += 5.588238 * frame;
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec4 center = texture(sceneColor, texCoord);

    // This frame's motion in pixels, scaled by the shutter and clamped to
    // the maximum radius without changing its direction.
    vec2 motion = texture(velocity, texCoord).xy * u_blur.frame.xy * u_blur.blur.x;
    float motionLength = length(motion);
    if (motionLength < 0.5)
    {
        fragColor = center;
        return;
    }
    motion *= min(motionLength, u_blur.blur.y) / motionLength;
    vec2 motionUv = motion * u_blur.frame.zw;

    int taps = clamp(int(u_blur.blur.z), 2, MAX_TAPS);
    float offset = interleaved_gradient_noise(gl_FragCoord.xy, u_blur.blur.w);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < taps; ++i)
    {
        // Spread the taps over [-0.5, 0.5] of the vector, centred on the
        // pixel, each shifted by the same sub-tap offset.
        float t = (float(i) + offset) / float(taps) - 0.5;
        sum += texture(sceneColor, texCoord + motionUv * t).rgb;
    }
    fragColor = vec4(sum / float(taps), center.a);
}
