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
