#version 450

// PBR metal/roughness surface: world-space position, normal and (with
// HAS_TANGENTS) tangent frame for the fragment stage, plus the camera
// position derived from the view matrix.
//
// Keywords (injected as defines by standard_material's template, see
// docs/shaders.md): HAS_TANGENTS declares the tangent attribute at
// location 3 and forwards it; without it the pipeline reads a
// position+uv+normal record and normal mapping is off.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
#ifdef HAS_TANGENTS
layout(location = 3) in vec4 tangent;
#endif

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec3 cameraPosition;
#ifdef HAS_TANGENTS
layout(location = 4) out vec4 worldTangent;
#endif

void main()
{
    vec4 world = u_draw.modelMatrix * vec4(position, 1.0);
    worldPosition = world.xyz;
    // The per-draw normal matrix is the inverse-transpose of the
    // model's 3x3, so non-uniform scale does not skew the normal.
    worldNormal = mat3(u_draw.normalMatrix) * normal;
#ifdef HAS_TANGENTS
    // The tangent is a direction along the surface, so it rides
    // the model matrix directly; the handedness sign passes
    // through in .w to rebuild the bitangent.
    worldTangent = vec4(mat3(u_draw.modelMatrix) * tangent.xyz, tangent.w);
#endif
    texCoord = uv;
    cameraPosition = camera_position();
    gl_Position = u_frame.projectionMatrix * u_frame.viewMatrix * world;
}
