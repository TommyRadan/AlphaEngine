// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

// Helpers for passes that sample the scene depth
// (frame_context::scene_depth_texture). The texture stores the non-linear
// window-space depth the scene and skybox passes leave in the HDR
// target's depth24 attachment. The engine's projections
// (core::math::perspective / ortho) put clip-space depth in [0, w] and
// the viewport depth range is [0, 1], so the stored value is the NDC z
// itself: a clip-space reprojection takes it unchanged (see the velocity
// pass).
#ifndef AE_DEPTH_UTILS_GLSL
#define AE_DEPTH_UTILS_GLSL

// Sampled scene depth ([0, 1]) -> positive view-space distance
// along the camera's forward axis, in [near_plane, far_plane].
// Inverts the perspective divide of core::math::perspective:
// d = f / (f - n) - fn / ((f - n) * v) solved for v.
// A background texel (the scene pass clears depth to 1.0) maps
// to far_plane, so a fog or fade driven by this value covers
// the sky as well as geometry.
float linearize_depth(float depth, float near_plane, float far_plane)
{
    return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
}

#endif // AE_DEPTH_UTILS_GLSL
