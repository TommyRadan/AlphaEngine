// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/premade_3d/polyhedron.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;

    // Regular icosahedron (12 vertices, 20 triangular faces) built on the
    // shared polyhedron generator with the canonical base table.
    struct icosahedron : public polyhedron
    {
        icosahedron(asset_cache& cache, material* mat, float radius = 1.0f, unsigned int detail = 0);
    };
} // namespace rendering_engine
