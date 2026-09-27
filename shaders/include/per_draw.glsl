// The per-draw block of every 3D renderable: the model matrix and its
// normal matrix, 128 bytes (rendering_engine::per_draw_payload). The
// depth-only shadow pipelines read the same block, so a renderable's
// per-draw data serves them unchanged.
//
// Where it lives depends on the backend define AE_PUSH_CONSTANTS, which
// create_library_shader_module sets for a device with push constants
// (Vulkan):
//   - with it, the block is the pipeline's push constants (the renderer's
//     per-draw push range, vertex + fragment, offset 0), pushed by the pass
//     before each draw;
//   - without it (OpenGL, whose ARB_gl_spirv has no push constants), it is
//     a uniform block in set 1, binding BINDING_PER_DRAW_MODEL, bound from
//     the per-draw ring at a dynamic offset.
// Two mat4s sit at the same offsets (0 and 64) under std430, the push-
// constant default, as under std140, so both read the same bytes.
//
// normalMatrix is the inverse-transpose of the model's upper-left 3x3,
// computed once per draw on the CPU (renderables/per_draw_ubo.hpp) and
// widened to a mat4 so the upload is a plain 64-byte copy; read it as
// mat3(u_draw.normalMatrix). It keeps shading normals perpendicular under
// non-uniform scale without an inverse() per vertex.
//
// A SKINNED variant's per-draw group also carries the joint palette at
// BINDING_PER_DRAW_JOINTS: one mat4 per joint, each mapping the mesh's
// bind-pose space onto the joint's current pose in the mesh node's space
// (renderables/model.hpp, set_joint_matrices). skin_matrix() blends the
// four joints a vertex names by its weights; the model matrix is applied
// on top as usual.
#ifndef AE_PER_DRAW_GLSL
#define AE_PER_DRAW_GLSL

#include "include/bindings.glsl"

#ifdef AE_PUSH_CONSTANTS
layout(push_constant) uniform PerDraw
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} u_draw;
#else
layout(set = 1, binding = BINDING_PER_DRAW_MODEL, std140) uniform PerDraw
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} u_draw;
#endif

#ifdef SKINNED
// Read-only, so the vertex stage needs no store / atomic support.
layout(set = 1, binding = BINDING_PER_DRAW_JOINTS, std430) readonly buffer JointMatrices
{
    mat4 jointMatrices[];
} u_joints;

// Linear blend skinning: the weighted sum of the vertex's four joint
// matrices. An index past the end of the palette (a mesh drawn with the
// wrong skin) is clamped to the last joint rather than read out of bounds.
mat4 skin_matrix(uvec4 joints, vec4 weights)
{
    uint last = uint(u_joints.jointMatrices.length()) - 1u;
    return weights.x * u_joints.jointMatrices[min(joints.x, last)] +
           weights.y * u_joints.jointMatrices[min(joints.y, last)] +
           weights.z * u_joints.jointMatrices[min(joints.z, last)] +
           weights.w * u_joints.jointMatrices[min(joints.w, last)];
}
#endif

#endif // AE_PER_DRAW_GLSL
