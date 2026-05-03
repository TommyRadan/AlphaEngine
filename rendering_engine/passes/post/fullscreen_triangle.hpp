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
 * @brief Shared vertex shader source for post-process passes.
 *
 * Emits a single oversized triangle that covers the entire screen
 * from three vertices indexed by @c gl_VertexID — no vertex buffer
 * needed. The triangle's clipped extent inside the viewport is the
 * full [-1, 1] NDC quad, so the fragment shader sees every pixel
 * exactly once. UVs are emitted in [0, 1] with origin at the bottom
 * left, matching the convention the engine's textures sample with.
 *
 * Every post pass should construct its pipeline against this source
 * and an effect-specific fragment shader. Used today by
 * @ref passthrough_pass; reused by future tonemap / bloom / FXAA
 * passes without re-authoring the geometry.
 */

#pragma once

#include <string>

namespace rendering_engine
{
    inline const std::string fullscreen_triangle_vertex_shader = R"vs(
        #version 330

        out vec2 texCoord;

        void main()
        {
            // Three vertices, clip-space coords (-1,-1) (3,-1) (-1,3).
            // The slice inside the viewport is the full [-1,1] quad.
            vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
            texCoord = p;
            gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
        }
)vs";
} // namespace rendering_engine
