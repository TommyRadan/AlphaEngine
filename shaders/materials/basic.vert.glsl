#version 450

// Unlit textured surface: position + uv through the camera and the
// per-draw model matrix.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 texCoord;

void main()
{
    texCoord = uv;
    mat4 MVP = u_frame.projectionMatrix * u_frame.viewMatrix * u_draw.modelMatrix;
    gl_Position = MVP * vec4(position, 1.0);
}
