// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/octahedron.hpp>

namespace rendering_engine
{
    namespace
    {
        // Canonical octahedron base vertices and faces.
        const std::vector<float> base_vertices = {
            1.0f,
            0.0f,
            0.0f,
            -1.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            -1.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            -1.0f,
        };

        const std::vector<uint32_t> base_indices = {
            0, 2, 4, 0, 4, 3, 0, 3, 5, 0, 5, 2, 1, 2, 5, 1, 5, 3, 1, 3, 4, 1, 4, 2,
        };
    } // namespace

    octahedron::octahedron(material* mat, float radius, unsigned int detail)
        : polyhedron{mat, base_vertices, base_indices, radius, detail}
    {
    }
} // namespace rendering_engine
