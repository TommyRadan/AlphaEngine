// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/tetrahedron.hpp>

namespace rendering_engine
{
    namespace
    {
        // Canonical tetrahedron base vertices and faces.
        const std::vector<float> base_vertices = {
            1.0f,
            1.0f,
            1.0f,
            -1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            -1.0f,
        };

        const std::vector<uint32_t> base_indices = {
            2,
            1,
            0,
            0,
            3,
            2,
            1,
            3,
            0,
            2,
            3,
            1,
        };
    } // namespace

    tetrahedron::tetrahedron(asset_cache& cache, material* mat, float radius, unsigned int detail)
        : polyhedron{cache, mat, base_vertices, base_indices, radius, detail}
    {
    }
} // namespace rendering_engine
