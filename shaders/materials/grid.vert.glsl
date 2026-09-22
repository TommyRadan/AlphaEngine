#version 450

// Analytic infinite grid. The vertex positions arrive already in clip
// space (xy in {-1, 3}, a fullscreen triangle), so the vertex stage only
// has to unproject the near / far plane points back into world space for
// the fragment ray-march.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 nearPoint;
layout(location = 1) out vec3 farPoint;
layout(location = 2) out vec3 cameraPoint;

vec3 unproject(vec2 ndc, float z, mat4 inverseViewProj)
{
    vec4 world = inverseViewProj * vec4(ndc, z, 1.0);
    return world.xyz / world.w;
}

void main()
{
    mat4 viewProj = u_frame.projectionMatrix * u_frame.viewMatrix;
    mat4 inverseViewProj = inverse(viewProj);

    vec2 ndc = position.xy;
    // GL-convention clip space: near plane at z = -1, far at z = 1.
    nearPoint = unproject(ndc, -1.0, inverseViewProj);
    farPoint = unproject(ndc, 1.0, inverseViewProj);
    cameraPoint = inverse(u_frame.viewMatrix)[3].xyz;

    gl_Position = vec4(ndc, 0.0, 1.0);
}
