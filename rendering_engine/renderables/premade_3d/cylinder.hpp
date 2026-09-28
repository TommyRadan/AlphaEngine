// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Cylinder (or truncated cone) primitive. Centered at the origin with its axis along +Y;
    // the height spans [-height/2, +height/2]. The torso (side wall) is
    // tessellated into radial_segments columns by height_segments rows
    // of quads with smooth radial normals derived from the slope between
    // the two radii. Unless open_ended, a top and a bottom cap fan are
    // appended with flat normals. A cap whose radius is 0 is skipped, so
    // a top radius of 0 yields a cone. Vertex format is position + uv +
    // normal + tangent with CCW outward winding.
    struct cylinder : public mesh_source
    {
        cylinder(asset_cache& cache,
                 material* mat,
                 float radius_top = 1.0f,
                 float radius_bottom = 1.0f,
                 float height = 1.0f,
                 unsigned int radial_segments = 32,
                 unsigned int height_segments = 1,
                 bool open_ended = false);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        float m_radius_top;
        float m_radius_bottom;
        float m_height;
        unsigned int m_radial_segments;
        unsigned int m_height_segments;
        bool m_open_ended;
    };
} // namespace rendering_engine
