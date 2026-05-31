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

#include <rendering_engine/renderables/premade_3d/icosahedron.hpp>

#include <cmath>

namespace rendering_engine
{
    namespace
    {
        // Golden ratio t = (1 + sqrt(5)) / 2, matching three.js
        // IcosahedronGeometry.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

        // Canonical three.js IcosahedronGeometry base vertices (12) and faces
        // (20). The polyhedron generator normalises these onto the sphere.
        const std::vector<float> base_vertices = {
            -1.0f, t,  0.0f, 1.0f, t,  0.0f, -1.0f, -t,    0.0f, 1.0f, -t,   0.0f, 0.0f, -1.0f, t,  0.0f, 1.0f, t, 0.0f,
            -1.0f, -t, 0.0f, 1.0f, -t, t,    0.0f,  -1.0f, t,    0.0f, 1.0f, -t,   0.0f, -1.0f, -t, 0.0f, 1.0f,
        };

        const std::vector<uint32_t> base_indices = {
            0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
            3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1,
        };
    } // namespace

    icosahedron::icosahedron(material* mat, float radius, unsigned int detail)
        : polyhedron{mat, base_vertices, base_indices, radius, detail}
    {
    }
} // namespace rendering_engine
