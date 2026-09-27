// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Instanced unlit geometry. The model matrix and colour arrive as
// ordinary vertex attributes from a per-instance stream (stepped once per
// instance) rather than being indexed by gl_InstanceIndex. Location 0 is
// the shared geometry position; the model matrix occupies four
// consecutive vec4 slots (one per column) and the tint follows.

#include "include/per_frame.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 model0;
layout(location = 2) in vec4 model1;
layout(location = 3) in vec4 model2;
layout(location = 4) in vec4 model3;
layout(location = 5) in vec4 instanceTint;

layout(location = 0) out vec4 instanceColor;

// The depth pre-pass runs this same module in a depth-only pipeline and
// the scene pass then compares against the depth it wrote, so the clip
// position must come out bit-identical in both pipelines.
invariant gl_Position;

void main()
{
    mat4 model = mat4(model0, model1, model2, model3);
    instanceColor = instanceTint;
    gl_Position = u_frame.viewProjectionMatrix * model * vec4(position, 1.0);
}
