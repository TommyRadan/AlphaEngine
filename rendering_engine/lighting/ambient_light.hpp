// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Uniform fill light that reaches every surface from all directions
    // equally — no position, no direction. The scene pass sums every
    // ambient light's @c color * @c intensity into a single ambient term
    // in the lights UBO.
    struct ambient_light : light
    {
        ambient_light();
    };
} // namespace rendering_engine
