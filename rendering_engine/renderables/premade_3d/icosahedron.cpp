// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/icosahedron.hpp>

#include <cmath>

namespace rendering_engine
{
    namespace
    {
        // Golden ratio t = (1 + sqrt(5)) / 2.
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

        // Canonical icosahedron base vertices (12) and faces
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
