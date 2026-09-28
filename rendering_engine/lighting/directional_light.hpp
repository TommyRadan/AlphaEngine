// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Light arriving as parallel rays from a constant direction,
    // independent of surface position — a distant sun. The rays travel
    // along its owner's world forward (+X) axis (see @ref place_light).
    struct directional_light : light
    {
        directional_light();

        // When true this light renders a shadow map from its point of
        // view and the lit materials sample it to occlude its
        // contribution. Only the first shadow-casting directional light
        // is honoured; the rest light without casting.
        bool cast_shadow{false};
    };
} // namespace rendering_engine
