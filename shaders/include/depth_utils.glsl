// Helpers for passes that sample the scene depth
// (frame_context::scene_depth_texture). The texture stores the non-linear
// window-space depth the scene and skybox passes leave in the HDR
// target's depth24 attachment: d = 0.5 * z_ndc + 0.5 in [0, 1], where
// z_ndc is the post-divide z of a GL-convention projection (clip z in
// [-w, w], the matrices core::math::perspective builds). Both backends
// store that same value - OpenGL through its default depth range and
// Vulkan through the [0, 1] viewport depth with VK_EXT_depth_clip_control
// mapping the [-1, 1] NDC range onto it - so shaders never need a
// per-backend branch.
#ifndef AE_DEPTH_UTILS_GLSL
#define AE_DEPTH_UTILS_GLSL

// Sampled scene depth ([0, 1]) -> NDC z ([-1, 1]), the value the
// projection produced after the perspective divide. This is the
// input a clip-space reprojection wants (see the velocity pass).
float depth_to_ndc(float depth)
{
    return depth * 2.0 - 1.0;
}

// Sampled scene depth ([0, 1]) -> positive view-space distance
// along the camera's forward axis, in [near_plane, far_plane].
// Inverts the perspective divide of core::math::perspective:
// z_ndc = (f + n) / (f - n) - 2fn / ((f - n) * v) solved for v.
// A background texel (the scene pass clears depth to 1.0) maps
// to far_plane, so a fog or fade driven by this value covers
// the sky as well as geometry.
float linearize_depth(float depth, float near_plane, float far_plane)
{
    return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));
}

#endif // AE_DEPTH_UTILS_GLSL
