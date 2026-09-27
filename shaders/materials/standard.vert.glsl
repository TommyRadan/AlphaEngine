#version 450

// PBR metal/roughness surface: world-space position, normal and (with
// HAS_TANGENTS) tangent frame for the fragment stage, plus the camera
// position the per-frame block carries.
//
// Keywords (injected as defines by standard_material's template, see
// docs/shaders.md): HAS_TANGENTS declares the tangent attribute at
// location 3 and forwards it; without it the pipeline reads a
// position+uv+normal record and normal mapping is off. SKINNED declares
// the joint indices (location 4) and weights (location 5) of the
// position+uv+normal+tangent+skin record and moves the vertex by the
// per-draw joint palette (include/per_draw.glsl) before the model matrix.

#include "include/per_frame.glsl"
#include "include/per_draw.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
#ifdef HAS_TANGENTS
layout(location = 3) in vec4 tangent;
#endif
#ifdef SKINNED
layout(location = 4) in uvec4 joints;
layout(location = 5) in vec4 weights;
#endif

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 texCoord;
layout(location = 3) out vec3 cameraPosition;
#ifdef HAS_TANGENTS
layout(location = 4) out vec4 worldTangent;
#endif

// The depth pre-pass runs this same module in a depth-only pipeline and
// the scene pass then compares against the depth it wrote, so the clip
// position must come out bit-identical in both pipelines.
invariant gl_Position;

void main()
{
#ifdef SKINNED
    // The palette maps the bind pose into the mesh node's space, so the
    // skinned vertex then rides the model matrix like any other. Joints
    // are rigid (rotation, translation, uniform scale), so mat3(skin)
    // carries the normal and tangent directions; the fragment stage
    // renormalises them.
    mat4 skin = skin_matrix(joints, weights);
    vec4 localPosition = skin * vec4(position, 1.0);
    vec3 localNormal = mat3(skin) * normal;
#else
    vec4 localPosition = vec4(position, 1.0);
    vec3 localNormal = normal;
#endif

    vec4 world = u_draw.modelMatrix * localPosition;
    worldPosition = world.xyz;
    // The per-draw normal matrix is the inverse-transpose of the
    // model's 3x3, so non-uniform scale does not skew the normal.
    worldNormal = mat3(u_draw.normalMatrix) * localNormal;
#ifdef HAS_TANGENTS
    // The tangent is a direction along the surface, so it rides
    // the model matrix directly; the handedness sign passes
    // through in .w to rebuild the bitangent.
#ifdef SKINNED
    vec3 localTangent = mat3(skin) * tangent.xyz;
#else
    vec3 localTangent = tangent.xyz;
#endif
    worldTangent = vec4(mat3(u_draw.modelMatrix) * localTangent, tangent.w);
#endif
    texCoord = uv;
    cameraPosition = u_frame.cameraPosition.xyz;
    gl_Position = u_frame.viewProjectionMatrix * world;
}
