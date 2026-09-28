// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// The per-draw block of every 3D renderable: the model matrix and its
// normal matrix, 128 bytes (rendering_engine::per_draw_payload). The
// depth-only shadow pipelines read the same block, so a renderable's
// per-draw data serves them unchanged, and so does every material
// template's, a game's included (see per_material.glsl for the whole
// binding model).
//
// The block is the pipeline's push constants (the renderer's per-draw
// push range, vertex + fragment, offset 0), pushed by the pass before each
// draw; its two mat4s sit at offsets 0 and 64.
//
// normalMatrix is the inverse-transpose of the model's upper-left 3x3,
// computed once per draw on the CPU (renderables/per_draw_ubo.hpp) and
// widened to a mat4 so the push is a plain 64-byte copy; read it as
// mat3(u_draw.normalMatrix). It keeps shading normals perpendicular under
// non-uniform scale without an inverse() per vertex.
//
// A SKINNED variant's per-draw group carries the joint palette at
// BINDING_PER_DRAW_JOINTS: one mat4 per joint, each mapping the mesh's
// bind-pose space onto the joint's current pose in the mesh node's space
// (renderables/model.hpp, set_joint_matrices). skin_matrix() blends the
// four joints a vertex names by its weights; the model matrix is applied
// on top as usual.
#ifndef AE_PER_DRAW_GLSL
#define AE_PER_DRAW_GLSL

#include "include/bindings.glsl"

layout(push_constant) uniform PerDraw
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} u_draw;

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
