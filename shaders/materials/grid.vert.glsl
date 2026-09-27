// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Analytic infinite grid. The vertex positions arrive already in clip
// space (xy in {-1, 3}, a fullscreen triangle), so the vertex stage only
// has to unproject the near / far plane points back into world space for
// the fragment ray-march.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;
layout(location = 2) out vec3 cameraPoint;

vec3 unproject(vec2 ndc, float z, mat4 inverseViewProj)
{
    vec4 world = inverseViewProj * vec4(ndc, z, 1.0);
    return world.xyz / world.w;
}

void main()
{
    vec2 ndc = position.xy;
    // NDC depth: near plane at z = 0, far at z = 1.
    nearPoint = unproject(ndc, 0.0, u_frame.inverseViewProjectionMatrix);
    farPoint = unproject(ndc, 1.0, u_frame.inverseViewProjectionMatrix);
    cameraPoint = u_frame.cameraPosition.xyz;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
