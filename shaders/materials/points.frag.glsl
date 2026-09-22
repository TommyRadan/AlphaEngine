#version 450

// Point sprites: the material colour times the per-vertex colour, with
// an optional sprite texture over gl_PointCoord.

#include "include/bindings.glsl"

layout(location = 0) in vec3 pointColor;
layout(location = 0) out vec4 fragColor;

// std140: vec4 color at 0, then float size, sizeAttenuation, useTexture
// at 16/20/24; 32 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 color;
    float size;
    float sizeAttenuation;
    float useTexture;
} u_material;

layout(set = 2, binding = BINDING_MATERIAL_ALBEDO_MAP) uniform sampler2D spriteTexture;

void main()
{
    vec4 base = u_material.color * vec4(pointColor, 1.0);
    if (u_material.useTexture != 0.0)
    {
        // gl_PointCoord runs [0,1] across the rasterized point
        // sprite, so a sprite texture maps straight onto it.
        base *= texture(spriteTexture, gl_PointCoord);
    }
    fragColor = base;
}
