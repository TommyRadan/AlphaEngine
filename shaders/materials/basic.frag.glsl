#version 450

// Unlit textured surface: a colour tint, optionally multiplied by the
// albedo map.

#include "include/bindings.glsl"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

// std140: vec4 color at 0, float useTexture at 16; 32 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 color;
    float useTexture;
} u_material;

layout(set = 2, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D albedoTexture;

void main()
{
    vec4 base = u_material.color;
    if (u_material.useTexture != 0.0)
    {
        base *= texture(albedoTexture, texCoord);
    }
    fragColor = base;
}
