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
    // Omni-directional light radiating from a world-space point and
    // falling off with distance. Three.js analog: THREE.PointLight.
    struct point_light : light
    {
        point_light();

        // World-space position the light radiates from.
        infrastructure::math::vec3 position{0.0f, 0.0f, 0.0f};

        // Distance past which the light contributes nothing. 0 means no
        // hard cutoff (falloff still applies). Three.js analog:
        // PointLight.distance.
        float range{0.0f};

        // Classic constant / linear / quadratic attenuation
        // coefficients: 1 / (constant + linear*d + quadratic*d*d).
        float constant_attenuation{1.0f};
        float linear_attenuation{0.0f};
        float quadratic_attenuation{1.0f};
    };
} // namespace rendering_engine
