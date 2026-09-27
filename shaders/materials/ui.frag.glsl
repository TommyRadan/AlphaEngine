// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#version 450

// UI compositing, straight onto the LDR swapchain: the texture modulated by
// the vertex colour. An image is tinted, a font atlas (white, coverage in
// alpha) takes the colour with its coverage as alpha, and the material's
// white texel reduces a flat quad to the colour itself. uv (0, 0) is the
// image's first row, which the y-down projection puts at the top.

layout(location = 0) in vec2 texCoord;
layout(location = 1) in vec4 tint;
layout(location = 0) out vec4 fragColor;

// Set 0 is the ui pass's UiFrame block at binding 0; the per-draw set
// carries only the texture, at a binding of its own.
layout(set = 1, binding = 1) uniform sampler2D tex;

void main()
{
    fragColor = texture(tex, texCoord) * tint;
}
