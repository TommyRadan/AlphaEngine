// The view-globals block the scene pass uploads once per frame into set 0,
// binding BINDING_PER_FRAME: everything a scene shader knows about the
// view it renders, in one buffer. This is the one declaration of that
// block; every scene shader includes it rather than restating the layout.
// The lights and the shadow data stay in their own blocks of the same set
// (lights.glsl, shadows.glsl).
//
// std140, 560 bytes, mirroring rendering_engine::view_globals
// (rendering_engine/passes/view_globals.hpp) byte-for-byte:
//     0   mat4 viewMatrix
//    64   mat4 projectionMatrix             carries the temporal-AA jitter
//   128   mat4 viewProjectionMatrix         projectionMatrix * viewMatrix
//   192   mat4 inverseViewMatrix
//   256   mat4 inverseProjectionMatrix
//   320   mat4 inverseViewProjectionMatrix
//   384   mat4 prevViewProjectionMatrix     previous frame's, unjittered
//   448   vec4 cameraPosition               xyz world position, w = 1
//   464   vec4 viewport                     xy size in pixels, zw reciprocal
//   480   vec4 time                         x seconds since start, y frame
//                                           delta in seconds (z, w unused)
//   496   vec4 jitter                       xy the NDC jitter in
//                                           projectionMatrix, zw last frame's
//   512   vec4 fogColor                     rgb colour, a = mode (0 none,
//                                           1 linear, 2 exp2)
//   528   vec4 fogParams                    x near, y far, z density,
//                                           w height density
//   544   vec4 heightFogParams              x falloff, y reference height
//                                           (z, w unused)
#ifndef AE_PER_FRAME_GLSL
#define AE_PER_FRAME_GLSL

#include "include/bindings.glsl"

layout(set = 0, binding = BINDING_PER_FRAME, std140) uniform ViewGlobals
{
    mat4 viewMatrix;
    mat4 projectionMatrix;
    mat4 viewProjectionMatrix;
    mat4 inverseViewMatrix;
    mat4 inverseProjectionMatrix;
    mat4 inverseViewProjectionMatrix;
    mat4 prevViewProjectionMatrix;
    vec4 cameraPosition;
    vec4 viewport;
    vec4 time;
    vec4 jitter;
    vec4 fogColor;
    vec4 fogParams;
    vec4 heightFogParams;
} u_frame;

#endif // AE_PER_FRAME_GLSL
