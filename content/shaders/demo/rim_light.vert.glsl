// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// The rim-light material of the custom-material showcase
// (external/custom_material_demo_module.cpp): an asset shader, read from
// the content directory's shaders/, that includes the engine's per-frame
// and per-draw headers the way a built-in material does. Position, uv and
// normal through the camera and the per-draw model matrix.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;

// The depth pre-pass runs this same module in a depth-only pipeline and
// the scene pass then compares against the depth it wrote, so the clip
// position must come out bit-identical in both pipelines.
invariant gl_Position;

void main()
{
    vec4 world = u_draw.modelMatrix * vec4(position, 1.0);
    worldPosition = world.xyz;
    worldNormal = mat3(u_draw.normalMatrix) * normal;
    texCoord = uv;
    gl_Position = u_frame.viewProjectionMatrix * world;
}
