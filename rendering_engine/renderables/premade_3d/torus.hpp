// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Torus (donut) surface.
    // @c radius is the distance from the centre of the torus to the centre
    // of the tube, @c tube is the radius of the tube itself. @c radial_segments
    // controls the tessellation around the tube cross-section, @c tubular_segments
    // the tessellation around the main ring, and @c arc sweeps a partial ring
    // (full ring at 2*pi). Each cell is two triangles. Vertex format is
    // position + uv + normal + tangent with outward-facing CCW winding.
    struct torus : public mesh_source
    {
        torus(asset_cache& cache,
              material* mat,
              float radius = 1.0f,
              float tube = 0.4f,
              unsigned int radial_segments = 12,
              unsigned int tubular_segments = 48,
              float arc = 6.28318530718f /* 2*pi */);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_radius;
        float m_tube;
        unsigned int m_radial_segments;
        unsigned int m_tubular_segments;
        float m_arc;
    };
} // namespace rendering_engine
