#version 450

// Depth-only stage shared by the directional shadow pass and each face of
// the omni shadow pass: the caster's world position through the light's
// view-projection. The per-draw block is the same one every renderable
// binds for the scene pass, so their bind groups bind here unchanged.

#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;

// Pass-private: the light's view-projection numbers locally from 0 in
// set 0 (it shares OpenGL's UBO namespace with the per-draw block at 1).
layout(set = 0, binding = 0, std140) uniform LightFrame
{
    mat4 lightViewProj;
} u_light;

void main()
{
    gl_Position = u_light.lightViewProj * u_draw.modelMatrix * vec4(position, 1.0);
}
