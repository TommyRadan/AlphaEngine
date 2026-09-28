// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Flat subdivided plane lying in the XY plane, centred on the origin.
    // Parameterised by (width, height, width_segments, height_segments).
    // Vertex format is position + uv + normal + tangent; every
    // normal points along +Z and UVs span [0, 1] across the surface.
    struct plane : public mesh_source
    {
        plane(asset_cache& cache,
              material* mat,
              float width = 1.0f,
              float height = 1.0f,
              unsigned int width_segments = 1,
              unsigned int height_segments = 1);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_width;
        float m_height;
        unsigned int m_width_segments;
        unsigned int m_height_segments;
    };
} // namespace rendering_engine
