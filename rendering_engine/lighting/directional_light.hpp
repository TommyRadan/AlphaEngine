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

#include <infrastructure/math/math.hpp>
#include <rendering_engine/lighting/light.hpp>

namespace rendering_engine
{
    // Light arriving as parallel rays from a constant direction,
    // independent of surface position — a distant sun. Three.js analog:
    // THREE.DirectionalLight.
    struct directional_light : light
    {
        directional_light();

        // World-space direction the light travels along (from the
        // source toward the scene). Need not be normalized; the scene
        // pass normalizes before packing it into the UBO.
        infrastructure::math::vec3 direction{0.0f, 0.0f, -1.0f};

        // When true this light renders a shadow map from its point of
        // view and the lit materials sample it to occlude its
        // contribution. Three.js analog: Light.castShadow. Only the
        // first shadow-casting directional light is honoured today; the
        // rest light without casting. Defaults to false so existing
        // scenes keep their current look until they opt in.
        bool cast_shadow{false};
    };
} // namespace rendering_engine
