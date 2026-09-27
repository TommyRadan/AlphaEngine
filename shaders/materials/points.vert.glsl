// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Point sprites with a per-vertex colour and an optional depth-attenuated
// size.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 pointColor;

// The depth pre-pass runs this same module in a depth-only pipeline and
// the scene pass then compares against the depth it wrote, so the clip
// position, and with it the sprite size that decides which pixels a
// point covers, must come out bit-identical in both pipelines.
invariant gl_Position;
invariant gl_PointSize;

// std140: vec4 color at 0, then float size, sizeAttenuation, useTexture
// at 16/20/24; 32 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 color;
    float size;
    float sizeAttenuation;
    float useTexture;
} u_material;

void main()
{
    pointColor = color;
    vec4 viewPosition = u_frame.viewMatrix * u_draw.modelMatrix * vec4(position, 1.0);
    gl_Position = u_frame.projectionMatrix * viewPosition;

    // View space looks down -z, so -viewPosition.z is the
    // forward distance. With attenuation the size is the sprite
    // diameter at one unit of depth and shrinks with distance;
    // without it the size is a constant pixel diameter.
    float depth = max(-viewPosition.z, 0.0001);
    gl_PointSize = u_material.sizeAttenuation != 0.0
        ? u_material.size / depth
        : u_material.size;
}
