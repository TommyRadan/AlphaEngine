// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/math/easing.hpp>

#include <algorithm>
#include <cmath>

#include <core/math/constants.hpp>

namespace core::math
{
    namespace
    {
        // The overshoot of the back family: about 10% past the target, the
        // amount Robert Penner's reference equations use.
        constexpr float back_overshoot = 1.70158f;

        float out_bounce_curve(float t)
        {
            // Four parabolic arcs of decreasing height; 7.5625 and 2.75 are
            // the reference constants that make the arcs meet at y = 1.
            constexpr float n = 7.5625f;
            constexpr float d = 2.75f;
            if (t < 1.0f / d)
            {
                return n * t * t;
            }
            if (t < 2.0f / d)
            {
                t -= 1.5f / d;
                return n * t * t + 0.75f;
            }
            if (t < 2.5f / d)
            {
                t -= 2.25f / d;
                return n * t * t + 0.9375f;
            }
            t -= 2.625f / d;
            return n * t * t + 0.984375f;
        }
    } // namespace

    float ease(easing kind, float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (kind)
        {
        case easing::linear:
            return t;
        case easing::in_quad:
            return t * t;
        case easing::out_quad:
            return 1.0f - (1.0f - t) * (1.0f - t);
        case easing::in_out_quad:
            return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        case easing::in_cubic:
            return t * t * t;
        case easing::out_cubic:
        {
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case easing::in_out_cubic:
        {
            if (t < 0.5f)
            {
                return 4.0f * t * t * t;
            }
            const float u = 1.0f - t;
            return 1.0f - 4.0f * u * u * u;
        }
        case easing::in_sine:
            return 1.0f - std::cos(t * pi * 0.5f);
        case easing::out_sine:
            return std::sin(t * pi * 0.5f);
        case easing::in_out_sine:
            return 0.5f - 0.5f * std::cos(t * pi);
        case easing::in_expo:
            // The exponential never reaches 0 on its own; pin the end points.
            return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case easing::out_expo:
            return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case easing::in_out_expo:
            if (t <= 0.0f || t >= 1.0f)
            {
                return t;
            }
            return t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f)
                            : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
        case easing::in_back:
            return t * t * ((back_overshoot + 1.0f) * t - back_overshoot);
        case easing::out_back:
        {
            const float u = t - 1.0f;
            return 1.0f + u * u * ((back_overshoot + 1.0f) * u + back_overshoot);
        }
        case easing::in_out_back:
        {
            // The in-out variant scales the overshoot so each half keeps the
            // same visual amount as the one-sided curves.
            constexpr float s = back_overshoot * 1.525f;
            if (t < 0.5f)
            {
                const float u = 2.0f * t;
                return 0.5f * u * u * ((s + 1.0f) * u - s);
            }
            const float u = 2.0f * t - 2.0f;
            return 0.5f * (u * u * ((s + 1.0f) * u + s) + 2.0f);
        }
        case easing::in_bounce:
            return 1.0f - out_bounce_curve(1.0f - t);
        case easing::out_bounce:
            return out_bounce_curve(t);
        case easing::in_out_bounce:
            return t < 0.5f ? 0.5f * (1.0f - out_bounce_curve(1.0f - 2.0f * t))
                            : 0.5f * (1.0f + out_bounce_curve(2.0f * t - 1.0f));
        }
        return t;
    }
} // namespace core::math
