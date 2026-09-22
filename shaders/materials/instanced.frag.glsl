#version 450

// Instanced unlit geometry: the material tint times the per-instance
// colour.

#include "include/bindings.glsl"

layout(location = 0) in vec4 instanceColor;
layout(location = 0) out vec4 fragColor;

// std140: a single vec4 tint, 16 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 color;
} u_material;

void main()
{
    fragColor = u_material.color * instanceColor;
}
