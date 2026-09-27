// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// A selectable tonemap curve followed by an explicit gamma-2.2 encode.
// The LDR target and the swapchain are plain UNORM images (no sRGB
// format, so no encode on store), so the framebuffer is treated as linear
// and the encode has to happen here. Gamma 2.2 is visually indistinguishable
// from the piecewise sRGB curve at these magnitudes and is the standard
// chain partner for these operators.
//
// u_tonemap.op selects the curve and matches the
// rendering_engine::tonemap_operator enumerator values exactly:
// 0 = none (clamp only), 1 = Reinhard, 2 = ACES filmic.
//
// tonemap_pass builds four variants from two keywords, so an effect that
// is off costs nothing:
//   USE_AUTO_EXPOSURE  the exposure comes from auto_exposure_pass's 1x1
//                      result (g = log2 exposure) instead of
//                      u_tonemap.exposure.
//   USE_COLOR_GRADING  the display-referred colour, after the gamma
//                      encode, goes through the strip lookup table and is
//                      blended with the ungraded colour by
//                      u_tonemap.gradingIntensity.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// std140 rounds the { float, int, float } block up to the 16-byte
// minimum; exposure sits at offset 0, op at 4 and gradingIntensity at 8.
layout(set = 0, binding = 1, std140) uniform Tonemap
{
    float exposure;
    int op;
    float gradingIntensity;
} u_tonemap;

#ifdef USE_COLOR_GRADING
// The N^2 x N strip LUT (see rendering_engine::color_grading_settings):
// N slices of N x N texels side by side, red across a slice, green down
// it (row 0, the image's top row, is green 0) and blue slice by slice.
layout(set = 0, binding = 2) uniform sampler2D gradingLut;

// Trilinear lookup of @p color (display-referred, [0, 1]) in the strip:
// the hardware filters red and green bilinearly inside a slice, with the
// coordinates kept on the outer texel centres so no tap bleeds into the
// neighbouring slice, and the two slices around blue are blended here.
vec3 apply_grading_lut(vec3 color)
{
    float size = float(textureSize(gradingLut, 0).y);
    vec3 scaled = clamp(color, 0.0, 1.0) * (size - 1.0);
    float slice = floor(scaled.b);
    float nextSlice = min(slice + 1.0, size - 1.0);
    vec2 inSlice = vec2((scaled.r + 0.5) / (size * size), (scaled.g + 0.5) / size);
    vec3 lower = textureLod(gradingLut, inSlice + vec2(slice / size, 0.0), 0.0).rgb;
    vec3 upper = textureLod(gradingLut, inSlice + vec2(nextSlice / size, 0.0), 0.0).rgb;
    return mix(lower, upper, scaled.b - slice);
}
#endif

#ifdef USE_AUTO_EXPOSURE
// auto_exposure_pass's adaptation result: r = adapted EV100, g = log2 of
// the exposure to apply.
layout(set = 0, binding = 3) uniform sampler2D adaptedExposure;
#endif

// Krzysztof Narkowicz's ACES filmic approximation.
vec3 aces_filmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard's x / (1 + x) shoulder.
vec3 reinhard(vec3 x)
{
    return x / (1.0 + x);
}

void main()
{
#ifdef USE_AUTO_EXPOSURE
    float exposure = exp2(texelFetch(adaptedExposure, ivec2(0, 0), 0).g);
#else
    float exposure = u_tonemap.exposure;
#endif
    vec3 hdr = texture(sceneColor, texCoord).rgb * exposure;
    vec3 mapped;
    if (u_tonemap.op == 2)
    {
        mapped = aces_filmic(hdr);
    }
    else if (u_tonemap.op == 1)
    {
        mapped = reinhard(hdr);
    }
    else
    {
        mapped = hdr;
    }
    vec3 ldr = clamp(mapped, 0.0, 1.0);
    vec3 srgb = pow(ldr, vec3(1.0 / 2.2));
#ifdef USE_COLOR_GRADING
    srgb = mix(srgb, apply_grading_lut(srgb), u_tonemap.gradingIntensity);
#endif
    fragColor = vec4(srgb, 1.0);
}
