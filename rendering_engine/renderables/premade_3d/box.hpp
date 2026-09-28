// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Parameterized box centered at the origin: per-axis dimensions plus
    // per-axis segment counts.
    // Each of the six faces is a tessellated grid with its own [0,1]
    // UVs and a constant outward-pointing normal, so the box can be
    // textured and normal-mapped. Vertex format is
    // position + uv + normal + tangent; tangents are derived from the
    // position/uv/normal channels via @ref generate_tangents.
    struct box : public mesh_source
    {
        box(asset_cache& cache,
            material* mat,
            float width = 1.0f,
            float height = 1.0f,
            float depth = 1.0f,
            unsigned int width_segments = 1,
            unsigned int height_segments = 1,
            unsigned int depth_segments = 1);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_width;
        float m_height;
        float m_depth;
        unsigned int m_width_segments;
        unsigned int m_height_segments;
        unsigned int m_depth_segments;
    };
} // namespace rendering_engine
