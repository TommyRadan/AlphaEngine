// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/premade_3d/polyhedron.hpp>

namespace rendering_engine
{
    struct material;

    // Regular tetrahedron (4 vertices, 4 triangular faces) built on the shared
    // polyhedron generator with the canonical base table.
    struct tetrahedron : public polyhedron
    {
        explicit tetrahedron(material* mat, float radius = 1.0f, unsigned int detail = 0);
    };
} // namespace rendering_engine
