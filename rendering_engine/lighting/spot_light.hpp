// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Cone-shaped light radiating from a world-space point along a
    // direction, falling off with distance like a point_light and with
    // angle within a cone.
    struct spot_light : light
    {
        spot_light();

        // World-space position the light shines from.
        core::math::vec3 position{0.0f, 0.0f, 0.0f};

        // World-space direction the cone points along (from the source
        // toward the scene). Need not be normalized; the scene pass
        // normalizes before packing it into the UBO.
        core::math::vec3 direction{1.0f, 0.0f, 0.0f};

        // Distance past which the light contributes nothing. 0 means no
        // hard cutoff (falloff still applies), matching point_light::range.
        float range{0.0f};

        // Classic constant / linear / quadratic attenuation coefficients,
        // matching point_light: 1 / (constant + linear*d + quadratic*d*d).
        float constant_attenuation{1.0f};
        float linear_attenuation{0.0f};
        float quadratic_attenuation{1.0f};

        // Half-angle, in radians, of the outer cone past which the light
        // contributes nothing.
        float outer_angle{0.523598776f}; // 30 degrees

        // Half-angle, in radians, of the inner cone within which the light
        // is at full intensity; the shader smoothsteps the falloff between
        // this and outer_angle. Values at or past outer_angle collapse the
        // penumbra to a hard edge.
        float inner_angle{0.349065850f}; // 20 degrees

        // When true this light renders a shadow map from its point of view
        // and the lit materials sample it to occlude its contribution. Only
        // the first shadow-casting spot light is honoured; the rest
        // light without casting.
        bool cast_shadow{false};
    };
} // namespace rendering_engine
