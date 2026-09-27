// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/cone.hpp>

rendering_engine::cone::cone(material* mat,
                             float radius,
                             float height,
                             unsigned int radial_segments,
                             unsigned int height_segments,
                             bool open_ended)
    : cylinder{mat, 0.0f, radius, height, radial_segments, height_segments, open_ended}
{
}
