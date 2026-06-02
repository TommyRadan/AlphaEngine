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
#include <rendering_engine/debug/line_helper.hpp>

namespace rendering_engine
{
    struct directional_light;
}

namespace rendering_engine::debug
{
    // Gizmo for a directional light. Draws a small square facing the
    // light's travel direction at the world origin plus a ray along that
    // direction, both tinted with the light's colour. The geometry tracks
    // the light's direction / colour every frame, so the helper must not
    // outlive the light it points at.
    struct directional_light_helper : public line_helper
    {
        explicit directional_light_helper(const directional_light* light, float size = 1.0f);

    protected:
        void refresh() override;

    private:
        const directional_light* m_light;
        float m_size;

        // Last state the geometry was built from, so refresh() only
        // rebuilds when the light actually moves or changes colour.
        core::math::vec3 m_last_direction{0.0f, 0.0f, 0.0f};
        core::math::vec3 m_last_color{0.0f, 0.0f, 0.0f};
        bool m_built{false};
    };
} // namespace rendering_engine::debug
