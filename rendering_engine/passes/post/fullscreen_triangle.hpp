// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file fullscreen_triangle.hpp
 * @brief The shared vertex data of the fullscreen-triangle passes.
 *
 * A single oversized triangle covers the entire screen; its clipped
 * extent inside the viewport is the full [-1, 1] NDC quad, so a fragment
 * shader sees every pixel exactly once. The matching vertex stage is
 * @c shaders/passes/fullscreen.vert.glsl, which reads these three
 * vertices through @c layout(location = 0) in vec2 and emits UVs in
 * [0, 1] with the origin at the bottom left.
 *
 * The vertex buffer is created and owned per pass (tonemap, bloom,
 * FXAA, TAA, velocity, skybox): six floats fits in the same draw_call
 * window as the existing per-instance UBOs.
 */

#pragma once

#include <array>

namespace rendering_engine
{
    // Three vertices in clip space: (-1,-1), (3,-1), (-1,3). The
    // triangle's slice inside the [-1,1] viewport is the full quad,
    // so the fragment shader covers every pixel exactly once.
    inline constexpr std::array<float, 6> fullscreen_triangle_vertices = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
} // namespace rendering_engine
