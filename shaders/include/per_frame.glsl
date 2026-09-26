// The per-view block the scene pass uploads once per frame into set 0,
// binding BINDING_PER_FRAME. This is the one declaration of that block;
// every scene shader includes it rather than restating the layout.
//
// std140, 160 bytes, matching scene_pass::per_frame_ubo_size:
//     0   mat4 viewMatrix
//    64   mat4 projectionMatrix
//   128   vec4 fogColor    rgb colour, a = mode (0 none, 1 linear, 2 exp2)
//   144   vec4 fogParams   x near, y far, z density
#ifndef AE_PER_FRAME_GLSL
#define AE_PER_FRAME_GLSL

#include "include/bindings.glsl"

layout(set = 0, binding = BINDING_PER_FRAME, std140) uniform PerFrame
{
    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec4 fogColor;
    vec4 fogParams;
} u_frame;

// The camera's world-space position. The view matrix is rigid (a
// rotation R and a translation t from look_at), so its inverse is
// transpose(R) with translation -transpose(R) * t; that is the camera
// position, and it needs no general 4x4 inverse per vertex. GLSL's
// vector * matrix product is the transpose product.
vec3 camera_position()
{
    return -(u_frame.viewMatrix[3].xyz * mat3(u_frame.viewMatrix));
}

#endif // AE_PER_FRAME_GLSL
