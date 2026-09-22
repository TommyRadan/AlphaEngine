#version 450

// Composite: scale a blurred mip by its weight and emit it. The pipeline
// blends additively (one, one) so each level accumulates on top of the
// scene already in the target.

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D bloomColor;

layout(set = 0, binding = 1, std140) uniform Composite
{
    vec4 params; // x weight, yzw unused
} u_composite;

void main()
{
    vec3 color = texture(bloomColor, texCoord).rgb;
    fragColor = vec4(color * u_composite.params.x, 1.0);
}
