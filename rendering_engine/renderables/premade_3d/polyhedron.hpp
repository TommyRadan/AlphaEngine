// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Generic polyhedron generator.
    // Takes a base mesh expressed as a flat list of vertex positions (x, y, z
    // triplets) and a flat list of triangle indices, subdivides each base
    // triangle @c detail times, projects every resulting vertex onto the
    // sphere of the given @c radius, and derives smooth (sphere) normals plus
    // spherical UVs.
    //
    // Vertex format is position + uv + normal + tangent. Triangles are emitted with CCW
    // outward winding, matching the convention documented in sphere.cpp. The
    // four platonic-solid wrappers (tetrahedron, octahedron, icosahedron,
    // dodecahedron) feed canonical base tables into this generator.
    struct polyhedron : public mesh_source
    {
        polyhedron(asset_cache& cache,
                   material* mat,
                   std::vector<float> base_vertices,
                   std::vector<uint32_t> base_indices,
                   float radius = 1.0f,
                   unsigned int detail = 0);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        std::vector<float> m_base_vertices;
        std::vector<uint32_t> m_base_indices;
        float m_radius;
        unsigned int m_detail;
    };
} // namespace rendering_engine
