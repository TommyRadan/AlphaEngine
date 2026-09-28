// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// The colour tint of the post-tint showcase (external/post_tint_demo_module.cpp):
// a full-screen pass registered from a game module ahead of tonemap. It
// emits the tint colour over the shared fullscreen triangle; the pipeline
// blends (zero, src_color) and writes only rgb, so every HDR texel is
// multiplied by the tint in place and its alpha is left alone.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0, std140) uniform Tint
{
    vec4 color; // rgb = the multiplier, a unused
} u_tint;

void main()
{
    fragColor = vec4(u_tint.color.rgb, 1.0);
}
