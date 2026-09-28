// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/mesh_source.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Cubed sphere ("quad sphere"): six face grids of NxN quads, each
    // vertex pushed out to the unit sphere. Eliminates the pole pinch
    // of a UV sphere — there is no preferred axis, only six face
    // patches that meet at the cube's corners with bounded distortion
    // (~1.4x at corners). Pairs naturally with cube-map textures, which
    // are sampled by the surface normal and are also free of the polar
    // singularity.
    struct cubed_sphere : public mesh_source
    {
        // @p subdivisions is the per-face grid resolution (NxN quads).
        // Total mesh has 6*subdivisions*subdivisions quads = 12*N*N
        // triangles.
        cubed_sphere(asset_cache& cache, material* mat, unsigned int subdivisions = 32);

    private:
        // Fetches the geometry through @p cache; called by the constructor.
        void fetch_mesh(asset_cache& cache);

        unsigned int m_subdivisions;
    };
} // namespace rendering_engine
