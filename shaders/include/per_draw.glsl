// The per-draw block every 3D renderable binds in set 1, binding
// BINDING_PER_DRAW_MODEL: the model matrix and its normal matrix, std140,
// 128 bytes (rendering_engine::per_draw_payload). The depth-only shadow
// pipelines read the same block, which is what lets the renderables'
// per-draw bind groups bind there unchanged.
//
// normalMatrix is the inverse-transpose of the model's upper-left 3x3,
// computed once per draw on the CPU (renderables/per_draw_ubo.hpp) and
// widened to a mat4 so the upload is a plain 64-byte copy; read it as
// mat3(u_draw.normalMatrix). It keeps shading normals perpendicular under
// non-uniform scale without an inverse() per vertex.
#ifndef AE_PER_DRAW_GLSL
#define AE_PER_DRAW_GLSL

#include "include/bindings.glsl"

layout(set = 1, binding = BINDING_PER_DRAW_MODEL, std140) uniform PerDraw
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} u_draw;

#endif // AE_PER_DRAW_GLSL
