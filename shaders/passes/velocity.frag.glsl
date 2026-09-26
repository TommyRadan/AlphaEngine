#version 450

// Reconstructs each pixel's previous-frame screen position from depth.
// The depth was rasterised with the projection offset by this frame's
// temporal-AA jitter, so the pixel's clip-space point (NDC xy from the
// UV, NDC z from the depth buffer, GL convention, z in [-1, 1]) is first
// moved back by that jitter: that is the surface's true, unjittered
// position. It is then pushed through the baked reprojection matrix
// prevViewProj * inverse(curViewProj), both unjittered, which takes it to
// the previous frame's clip space directly; the perspective divide there
// yields the previous NDC and hence the previous UV. The motion vector is
// the delta between the two unjittered UVs, so the TAA resolve reads
// history at texCoord - velocity on the pixel grid it accumulates on: a
// static camera yields exactly zero motion whatever the jitter.
//
// Background pixels (depth at the far plane) reproject through the same
// matrix, so a panning or rotating camera still produces correct motion
// for the skybox.
//
// depth_to_ndc undoes the [0, 1] window mapping documented on
// frame_context::scene_depth_texture.

#include "include/depth_utils.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D sceneDepth;

layout(set = 0, binding = 1, std140) uniform Reprojection
{
    mat4 reprojection; // prevViewProj * inverse(curViewProj), unjittered
    vec4 jitter;       // xy = this frame's projection jitter in NDC
} u_reproj;

void main()
{
    float depth = texture(sceneDepth, texCoord).r;

    // Current pixel in NDC, with the raster jitter undone (clip space
    // before the divide is the same point scaled by w, which cancels in
    // the divide below).
    vec2 ndc_xy = texCoord * 2.0 - 1.0 - u_reproj.jitter.xy;
    vec3 ndc = vec3(ndc_xy, depth_to_ndc(depth));

    vec4 prev_clip = u_reproj.reprojection * vec4(ndc, 1.0);
    vec2 prev_ndc = prev_clip.xy / prev_clip.w;

    vec2 current_uv = ndc_xy * 0.5 + 0.5;
    vec2 prev_uv = prev_ndc * 0.5 + 0.5;

    fragColor = vec4(current_uv - prev_uv, 0.0, 0.0);
}
