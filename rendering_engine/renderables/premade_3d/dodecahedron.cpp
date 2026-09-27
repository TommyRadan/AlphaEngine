// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/dodecahedron.hpp>

#include <cmath>

namespace rendering_engine
{
    namespace
    {
        // Golden ratio t = (1 + sqrt(5)) / 2 and its reciprocal r = 1 / t.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        const float r = 1.0f / t;

        // Canonical dodecahedron base vertices (20). The
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

        // Canonical dodecahedron faces: 12 pentagons, each
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
