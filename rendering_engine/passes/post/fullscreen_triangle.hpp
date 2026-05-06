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
 * @brief Shared vertex shader source and vertex buffer for post-process passes.
 *
 * Emits a single oversized triangle that covers the entire screen
 * via three vertices fed through @c layout(location = 0) in vec2 pos.
 * The triangle's clipped extent inside the viewport is the full
 * [-1, 1] NDC quad, so the fragment shader sees every pixel exactly
 * once. UVs are emitted in [0, 1] with origin at the bottom left,
 * matching the convention the engine's textures sample with.
 *
 * The vertex buffer is created and owned per-pass (tonemap, future
 * bloom, FXAA, ...) — six floats fits in the same draw_call window
 * as the existing per-instance UBOs. We use an explicit attribute
 * instead of @c gl_VertexIndex because some OpenGL @c ARB_gl_spirv
 * specializers (NVIDIA on this hardware) silently drop draws whose
 * vertex shader has no input variables. Going through a real
 * attribute keeps the SPIR-V portable and the OpenGL backend happy.
 */

#pragma once

#include <array>
#include <string>

namespace rendering_engine
{
    inline const std::string fullscreen_triangle_vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec2 in_pos;
        layout(location = 0) out vec2 texCoord;

        void main()
        {
            texCoord = (in_pos + 1.0) * 0.5;
            gl_Position = vec4(in_pos, 0.0, 1.0);
        }
)vs";

    // Three vertices in clip space: (-1,-1), (3,-1), (-1,3). The
    // triangle's slice inside the [-1,1] viewport is the full quad,
    // so the fragment shader covers every pixel exactly once.
    inline constexpr std::array<float, 6> fullscreen_triangle_vertices = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
} // namespace rendering_engine
