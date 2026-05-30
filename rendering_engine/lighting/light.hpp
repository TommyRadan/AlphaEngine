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

#include <vector>

#include <infrastructure/math/math.hpp>

namespace rendering_engine
{
    // Discriminator for the concrete light kind. The scene pass reads
    // it to route each registered light into the matching slot of the
    // packed lights UBO without a dynamic_cast.
    enum class light_type
    {
        ambient,
        directional,
        point,
    };

    // Base for every light source. Carries the colour / intensity every
    // kind shares; concrete subtypes add their own geometry (direction,
    // position, attenuation). A light adds itself to the process-wide
    // registry on construction and removes itself on destruction, so
    // simply keeping a light alive is enough for the scene pass to see
    // it. Main-thread-only, like the rest of the engine — no
    // synchronization.
    struct light
    {
        explicit light(light_type type);
        virtual ~light();

        light(const light&) = delete;
        light& operator=(const light&) = delete;

        light_type type() const noexcept;

        // Linear RGB radiance. Multiplied by @ref intensity before
        // upload.
        infrastructure::math::vec3 color{1.0f, 1.0f, 1.0f};

        // Scalar multiplier on @ref color. Three.js analog:
        // Light.intensity.
        float intensity{1.0f};

    private:
        light_type m_type;
    };

    // The lights alive right now, in construction order. Owned by the
    // lights themselves (the vector holds non-owning back-pointers); the
    // scene pass walks it once per frame to pack the lights UBO.
    const std::vector<light*>& registered_lights();
} // namespace rendering_engine
