#version 450

// Unlit line list with a per-vertex colour.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

layout(location = 0) out vec3 lineColor;

void main()
{
    lineColor = color;
    mat4 MVP = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix;
    gl_Position = MVP * vec4(position, 1.0);
}
