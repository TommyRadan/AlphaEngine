// The per-draw block every 3D renderable binds in set 1, binding
// BINDING_PER_DRAW_MODEL: the model matrix, std140, 64 bytes. The
// depth-only shadow pipelines read the same block, which is what lets
// the renderables' per-draw bind groups bind there unchanged.
#ifndef AE_PER_DRAW_GLSL
#define AE_PER_DRAW_GLSL

#include "include/bindings.glsl"

layout(set = 1, binding = BINDING_PER_DRAW_MODEL, std140) uniform PerDraw
{
    mat4 modelMatrix;
} u_draw;

#endif // AE_PER_DRAW_GLSL
