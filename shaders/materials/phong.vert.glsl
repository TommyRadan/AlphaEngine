#version 450

// Blinn-Phong lit surface: world-space position and normal for the
// fragment stage, plus the camera position the per-frame block carries.

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
    // The per-draw normal matrix is the inverse-transpose of the
    // model's 3x3, so non-uniform scale does not skew the normal.
    worldNormal = mat3(u_draw.normalMatrix) * normal;
    texCoord = uv;
    cameraPosition = u_frame.cameraPosition.xyz;
    gl_Position = u_frame.viewProjectionMatrix * world;
}
