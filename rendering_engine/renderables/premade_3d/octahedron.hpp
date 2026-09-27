// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/premade_3d/polyhedron.hpp>

namespace rendering_engine
{
    struct material;

    // Regular octahedron (6 vertices, 8 triangular faces) built on the shared
    // polyhedron generator with the canonical base table.
    struct octahedron : public polyhedron
    {
        explicit octahedron(material* mat, float radius = 1.0f, unsigned int detail = 0);
    };
} // namespace rendering_engine
