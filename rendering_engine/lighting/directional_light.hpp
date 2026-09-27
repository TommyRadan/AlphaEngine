// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Light arriving as parallel rays from a constant direction,
    // independent of surface position — a distant sun.
    struct directional_light : light
    {
        directional_light();

        // World-space direction the light travels along (from the
        // source toward the scene). Need not be normalized; the scene
        // pass normalizes before packing it into the UBO.
        core::math::vec3 direction{0.0f, 0.0f, -1.0f};

        // When true this light renders a shadow map from its point of
        // view and the lit materials sample it to occlude its
        // contribution. Only the first shadow-casting directional light is honoured today; the
        // rest light without casting. Defaults to false so existing
        // scenes keep their current look until they opt in.
        bool cast_shadow{false};
    };
} // namespace rendering_engine
