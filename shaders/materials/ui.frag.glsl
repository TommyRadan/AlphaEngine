#version 450

// UI compositing: a flat colour or a texture (sampled with a flipped v so
// image rows read top-down), straight onto the LDR swapchain.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

// The ui material has no per-frame set, so its per-draw block and
// sampler number locally from 0 in set 0.
layout(set = 0, binding = 0, std140) uniform UiDraw
{
    float useTexture;
    vec4 color;
} u_draw;

layout(set = 0, binding = 1) uniform sampler2D tex;

void main()
{
    fragColor = (u_draw.useTexture != 0.0)
        ? texture(tex, vec2(texCoord.x, 1.0 - texCoord.y))
        : u_draw.color;
}
