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

#include <rendering_engine/renderables/premade_3d/dodecahedron.hpp>

#include <cmath>

namespace rendering_engine
{
    namespace
    {
        // Golden ratio t = (1 + sqrt(5)) / 2 and its reciprocal r = 1 / t,
        // matching three.js DodecahedronGeometry.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        const float r = 1.0f / t;

        // Canonical three.js DodecahedronGeometry base vertices (20). The
        // polyhedron generator normalises these onto the sphere.
        const std::vector<float> base_vertices = {
            // (+-1, +-1, +-1)
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            1.0f,
            1.0f,
            1.0f,
            // (0, +-r, +-t)
            0.0f,
            -r,
            -t,
            0.0f,
            -r,
            t,
            0.0f,
            r,
            -t,
            0.0f,
            r,
            t,
            // (+-r, +-t, 0)
            -r,
            -t,
            0.0f,
            -r,
            t,
            0.0f,
            r,
            -t,
            0.0f,
            r,
            t,
            0.0f,
            // (+-t, 0, +-r)
            -t,
            0.0f,
            -r,
            t,
            0.0f,
            -r,
            -t,
            0.0f,
            r,
            t,
            0.0f,
            r,
        };

        // Canonical three.js DodecahedronGeometry faces: 12 pentagons, each
        // triangulated into three triangles (36 triangles / 108 indices).
        const std::vector<uint32_t> base_indices = {
            3,  11, 7,  3,  7,  15, 3,  15, 13, 7,  19, 17, 7,  17, 6,  7,  6,  15, 17, 4,  8,  17, 8,  10, 17, 10, 6,
            8,  0,  16, 8,  16, 2,  8,  2,  10, 0,  12, 1,  0,  1,  18, 0,  18, 16, 6,  10, 2,  6,  2,  13, 6,  13, 15,
            2,  16, 18, 2,  18, 3,  2,  3,  13, 18, 1,  9,  18, 9,  11, 18, 11, 3,  4,  14, 12, 4,  12, 0,  4,  0,  8,
            11, 9,  5,  11, 5,  19, 11, 19, 7,  19, 5,  14, 19, 14, 4,  19, 4,  17, 1,  12, 14, 1,  14, 5,  1,  5,  9,
        };
    } // namespace

    dodecahedron::dodecahedron(material* mat, float radius, unsigned int detail)
        : polyhedron{mat, base_vertices, base_indices, radius, detail}
    {
    }
} // namespace rendering_engine
