/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <core/math/math.hpp>

namespace rendering_engine
{
    // Atmospheric distance fog applied by the lit materials. The analog
    // of @c THREE.Fog (linear) and @c THREE.FogExp2 (exponential): the
    // fragment blends toward @ref fog_settings::color by camera distance,
    // approximating aerial perspective cheaply with no extra pass.
    //
    // The active fog is scene-wide state set through
    // @ref context::set_fog; the scene pass packs it into the per-view
    // @c PerFrame UBO (slot 0, binding 0) each frame, and the lit
    // fragment shaders read it from there. Disabled by default
    // (@ref fog_mode::none).
    enum class fog_mode
    {
        // No fog; the lit shaders skip the blend entirely.
        none,

        // Linear ramp between @ref fog_settings::near_distance (no fog)
        // and @ref fog_settings::far_distance (full fog).
        linear,

        // Exponential-squared falloff driven by
        // @ref fog_settings::density (matches @c THREE.FogExp2).
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
    };
} // namespace rendering_engine
