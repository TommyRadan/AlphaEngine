// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Flat disc lying in the XY plane. Built from a
    // centre vertex and a triangle fan of rim vertices. Vertex format is
    // position + uv + normal + tangent; every normal points along +Z. Front faces are
    // CCW when viewed from +Z.
    struct circle : public mesh_source
    {
        circle(asset_cache& cache,
               material* mat,
               float radius = 1.0f,
               unsigned int segments = 32,
               float theta_start = 0.0f,
               float theta_length = 6.28318530718f);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_radius;
        unsigned int m_segments;
        float m_theta_start;
        float m_theta_length;
    };
} // namespace rendering_engine
