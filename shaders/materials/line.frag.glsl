#version 450

// Unlit line list: the material colour times the per-vertex colour.

#include "include/bindings.glsl"

layout(location = 0) in vec3 lineColor;
layout(location = 0) out vec4 fragColor;

// std140: a single vec4 color, 16 bytes.
layout(set = 2, binding = BINDING_MATERIAL_PARAMS, std140) uniform Material
{
    vec4 color;
} u_material;

void main()
{
    fragColor = u_material.color * vec4(lineColor, 1.0);
}
