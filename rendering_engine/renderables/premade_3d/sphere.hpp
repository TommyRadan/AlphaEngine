// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // UV-sphere with quad-based tessellation (each lat/lon cell is two
    // triangles). Vertex format is position + uv + normal + tangent. Texturing is
    // well-behaved away from the poles; near the poles UVs pinch but
    // per-vertex normals stay smooth.
    struct sphere : public mesh_source
    {
        sphere(asset_cache& cache, material* mat, unsigned int stacks = 64, unsigned int slices = 128);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        unsigned int m_stacks;
        unsigned int m_slices;
    };
} // namespace rendering_engine
