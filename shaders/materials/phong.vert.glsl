#version 450

// Blinn-Phong lit surface: world-space position and normal for the
// fragment stage, plus the camera position derived from the view matrix.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec3 cameraPosition;

void main()
{
    vec4 world = u_draw.modelMatrix * vec4(position, 1.0);
    worldPosition = world.xyz;
    // Inverse-transpose of the upper-left 3x3 so non-uniform
    // scale does not skew the shading normal.
    mat3 normalMatrix = transpose(inverse(mat3(u_draw.modelMatrix)));
    worldNormal = normalMatrix * normal;
    texCoord = uv;
    // Camera world position is the translation column of the
    // inverse view matrix; derived here so the shared per-frame
    // UBO need not carry it.
    cameraPosition = inverse(u_frame.viewMatrix)[3].xyz;
    gl_Position = u_frame.projectionMatrix * u_frame.viewMatrix * world;
}
