// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// UI quads in drawable pixels: (0, 0) is the top-left corner, +x right,
// +y down. Nothing size-dependent is baked into a vertex (see ui_vertex in
// rendering_engine/materials/ui_material.hpp): the pivot and the corner
// offset are each an anchor, a fraction of the drawable, plus a pixel
// offset, resolved here against the size the ui pass publishes, so
// anchored and stretched elements follow a resize without a re-upload.

layout(location = 0) in vec2 pivotAnchor;
layout(location = 1) in vec2 pivotOffset;
layout(location = 2) in vec2 cornerAnchor;
layout(location = 3) in vec2 cornerOffset;
layout(location = 4) in vec2 rotation; // (cos, sin); positive turns clockwise on screen
layout(location = 5) in vec2 uv;
layout(location = 6) in vec4 color;

layout(location = 0) out vec2 texCoord;
layout(location = 1) out vec4 tint;

// The ui pass's per-frame block: the pixel-space orthographic projection
// and the drawable size it was built for (xy; zw its reciprocal).
layout(set = 0, binding = 0, std140) uniform UiFrame
{
    mat4 projection;
    vec4 viewport;
} u_frame;

void main()
{
    // The pivot snaps to a whole pixel so text laid out on integer offsets
    // from it stays texel-aligned wherever its anchor lands.
    vec2 pivot = floor(pivotAnchor * u_frame.viewport.xy + pivotOffset + 0.5);
    vec2 corner = cornerAnchor * u_frame.viewport.xy + cornerOffset;
    vec2 turned = vec2(rotation.x * corner.x - rotation.y * corner.y, rotation.y * corner.x + rotation.x * corner.y);

    texCoord = uv;
    tint = color;
    gl_Position = u_frame.projection * vec4(pivot + turned, 0.0, 1.0);
}
