// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// Shared vertex stage of every fullscreen-triangle pass (the post chain
// and the skybox). Three clip-space vertices, (-1,-1), (3,-1), (-1,3),
// arrive through the attribute in fullscreen_triangle.hpp; the
// triangle's slice inside the [-1, 1] viewport is the full quad, so the
// fragment stage sees every pixel exactly once. UVs are emitted in
// [0, 1] with the origin at the bottom left, matching the convention the
// engine's textures sample with.

layout(location = 0) in vec2 in_pos;
layout(location = 0) out vec2 texCoord;

void main()
{
    texCoord = (in_pos + 1.0) * 0.5;
    gl_Position = vec4(in_pos, 0.0, 1.0);
}
