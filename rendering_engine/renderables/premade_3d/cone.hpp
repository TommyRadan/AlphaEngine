// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/renderables/premade_3d/cylinder.hpp>

namespace rendering_engine
{
    struct material;

    // Cone primitive. A cone is just a
    // cylinder with a top radius of 0, so this is a thin wrapper that
    // forwards to the cylinder generator with radius_top fixed at 0.
    struct cone : public cylinder
    {
        explicit cone(material* mat,
                      float radius = 1.0f,
                      float height = 1.0f,
                      unsigned int radial_segments = 32,
                      unsigned int height_segments = 1,
                      bool open_ended = false);
    };
} // namespace rendering_engine
