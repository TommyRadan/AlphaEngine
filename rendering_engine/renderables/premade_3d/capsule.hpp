// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Capsule aligned to +Y and centred at the origin: a straight
    // cylindrical body of height @c length (spanning [-length/2,
    // +length/2]) capped by two hemispheres of @c radius. The surface is
    // generated as a single seamless lat/lon grid — the top hemisphere,
    // the cylindrical body, and the bottom hemisphere share rings so
    // there are no cracks. Vertex format is position + uv + normal + tangent; UVs
    // wrap radially and run along the axis. Parameterised by
    // (radius, length, cap_segments, radial_segments).
    struct capsule : public mesh_source
    {
        capsule(asset_cache& cache,
                material* mat,
                float radius = 0.5f,
                float length = 1.0f,
                unsigned int cap_segments = 8,
                unsigned int radial_segments = 16);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_radius;
        float m_length;
        unsigned int m_cap_segments;
        unsigned int m_radial_segments;
    };
} // namespace rendering_engine
