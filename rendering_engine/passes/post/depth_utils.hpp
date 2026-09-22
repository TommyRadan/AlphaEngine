/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file depth_utils.hpp
 * @brief Shared GLSL helpers for passes that sample the scene depth.
 *
 * @ref frame_context::scene_depth_texture stores the non-linear window-space
 * depth the scene and skybox passes leave in the HDR target's depth24
 * attachment: @c d = 0.5 * z_ndc + 0.5 in [0, 1], where @c z_ndc is the
 * post-divide z of a GL-convention projection (clip z in [-w, w], the
 * matrices @c core::math::perspective builds). Both backends store that
 * same value — OpenGL through its default depth range and Vulkan through
 * the [0, 1] viewport depth with VK_EXT_depth_clip_control mapping the
 * [-1, 1] NDC range onto it — so shaders never need a per-backend branch.
 *
 * The snippet below is spliced into a fragment shader's source between
 * its declarations and @c main (it carries no @c #version line). It is a
 * C++ string constant for now because the shader compiler takes whole
 * sources; once shader includes exist it moves verbatim into a
 * @c depth_utils.glsl file.
 */

#pragma once

#include <string_view>

namespace rendering_engine
{
    inline constexpr std::string_view depth_utils_glsl = R"glsl(
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
)glsl";
} // namespace rendering_engine
