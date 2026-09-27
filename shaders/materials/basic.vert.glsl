// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Unlit textured surface: position + uv through the camera and the
// per-draw model matrix.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 texCoord;

// The depth pre-pass runs this same module in a depth-only pipeline and
// the scene pass then compares against the depth it wrote, so the clip
// position must come out bit-identical in both pipelines.
invariant gl_Position;

void main()
{
    texCoord = uv;
    mat4 MVP = u_frame.viewProjectionMatrix * u_draw.modelMatrix;
    gl_Position = MVP * vec4(position, 1.0);
}
