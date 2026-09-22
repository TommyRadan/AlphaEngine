#version 450

// Reconstructs each pixel's previous-frame screen position from depth.
// The current pixel's clip-space point (NDC xy from the UV, NDC z from
// the depth buffer, GL convention, z in [-1, 1]) is pushed through the
// baked reprojection matrix prevViewProj * inverse(curViewProj), which
// takes it to the previous frame's clip space directly; the perspective
// divide there yields the previous NDC and hence the previous UV. The
// motion vector is the UV delta, so the TAA resolve reads history at
// texCoord - velocity.
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
} u_reproj;

void main()
{
    float depth = texture(sceneDepth, texCoord).r;

    // Current pixel in NDC (clip space before the divide is the same
    // point scaled by w, which cancels in the divide below).
    vec3 ndc = vec3(texCoord * 2.0 - 1.0, depth_to_ndc(depth));

    vec4 prev_clip = u_reproj.reprojection * vec4(ndc, 1.0);
    vec2 prev_ndc = prev_clip.xy / prev_clip.w;
    vec2 prev_uv = prev_ndc * 0.5 + 0.5;

    fragColor = vec4(texCoord - prev_uv, 0.0, 0.0);
}
