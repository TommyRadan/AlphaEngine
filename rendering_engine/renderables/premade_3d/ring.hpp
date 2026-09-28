// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Flat annulus lying in the XY plane. Built from a
    // radial/angular grid between an inner and outer radius. Vertex format is
    // position + uv + normal + tangent; every normal points along +Z. Front faces are
    // CCW when viewed from +Z.
    struct ring : public mesh_source
    {
        ring(asset_cache& cache,
             material* mat,
             float inner_radius = 0.5f,
             float outer_radius = 1.0f,
             unsigned int theta_segments = 32,
             unsigned int phi_segments = 1,
             float theta_start = 0.0f,
             float theta_length = 6.28318530718f);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_inner_radius;
        float m_outer_radius;
        unsigned int m_theta_segments;
        unsigned int m_phi_segments;
        float m_theta_start;
        float m_theta_length;
    };
} // namespace rendering_engine
