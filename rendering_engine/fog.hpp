// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>

namespace rendering_engine
{
    // Atmospheric distance fog applied by the lit materials. The
    // fragment blends toward @ref fog_settings::color by camera distance,
    // approximating aerial perspective cheaply with no extra pass. Two
    // modes: linear interpolation between @ref fog_settings::near_distance
    // and @ref fog_settings::far_distance, or exponential-squared falloff
    // driven by @ref fog_settings::density.
    //
    // The active fog is scene-wide state set through
    // @ref renderer::set_fog; the scene pass packs it into the per-view
    // @ref view_globals block (slot 0, binding 0) each frame, and the lit
    // fragment shaders read it from there. Disabled by default
    // (@ref fog_mode::none).
    enum class fog_mode
    {
        // No distance-fog term; the lit shaders skip that blend, but
        // @ref fog_settings::height_density still applies its own
        // analytic height-fog term regardless of mode.
        none,

        // Linear ramp between @ref fog_settings::near_distance (no fog)
        // and @ref fog_settings::far_distance (full fog).
        linear,

        // Exponential-squared fog: @c factor = @c exp(-(density *
        // distance)^2), driven by @ref fog_settings::density.
        exponential,
    };

    // Scene-wide fog parameters. @ref near_distance / @ref far_distance
    // drive @ref fog_mode::linear; @ref density drives
    // @ref fog_mode::exponential. The unused term for a given mode is
    // simply ignored by the shader. Members avoid the names @c near /
    // @c far because @c <windows.h> defines those as macros.
    struct fog_settings
    {
        fog_mode mode{fog_mode::none};

        // Linear RGB the scene fades into with distance.
        core::math::vec3 color{0.5f, 0.5f, 0.5f};

        // Linear fog: distance at which fog begins (0 contribution) and
        // the distance at which it fully saturates. World units.
        float near_distance{1.0f};
        float far_distance{100.0f};

        // Exponential fog: larger values thicken the fog faster.
        float density{0.02f};

        // Analytic exponential height fog (Unreal's Exponential Height
        // Fog). Applies independently of @ref mode, including
        // @ref fog_mode::none — it can run standalone or layered on top
        // of whichever distance term the mode selects. Density falls
        // off with world-Z altitude as height_density *
        // exp(-height_falloff * (z - reference_height)), so it reads as
        // height_density right at reference_height and thins out above
        // it. height_density of 0 (the default) disables the term
        // entirely, whatever @ref mode is, keeping today's distance-only
        // output bit-identical. The engine is +Z up, so "height" is
        // world Z. This is also the medium the volumetric fog pass
        // (@ref post_settings::volumetric) raymarches and lights; while
        // it runs, the analytic term only covers the part of each view
        // ray beyond its max distance.
        float height_density{0.0f};
        float height_falloff{0.1f};
        float reference_height{0.0f};
    };
} // namespace rendering_engine
