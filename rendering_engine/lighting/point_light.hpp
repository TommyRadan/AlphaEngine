// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Omni-directional light radiating from a point and falling off with
    // distance. The point is its owner's world position (see
    // @ref place_light).
    struct point_light : light
    {
        point_light();

        // Distance past which the light contributes nothing. 0 means no
        // hard cutoff (falloff still applies).
        float range{0.0f};

        // Classic constant / linear / quadratic attenuation
        // coefficients: 1 / (constant + linear*d + quadratic*d*d).
        float constant_attenuation{1.0f};
        float linear_attenuation{0.0f};
        float quadratic_attenuation{1.0f};

        // When true this light renders an omni (six-face) shadow map from its
        // position and the lit materials sample it to occlude its contribution.
        // Only the first shadow-casting point light is honoured; the rest
        // light without casting.
        bool cast_shadow{false};
    };
} // namespace rendering_engine
