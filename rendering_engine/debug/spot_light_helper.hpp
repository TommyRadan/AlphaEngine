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
    struct spot_light;
}

namespace rendering_engine::debug
{
    // Gizmo for a spot light. Draws a wireframe cone from the light's
    // world position along its direction, opening to the outer cone
    // half-angle at a fixed visual length, tinted with the light's
    // colour. The geometry tracks the light's position / direction /
    // colour / outer angle every frame, so the helper must not outlive
    // the light it points at.
    struct spot_light_helper : public line_helper
    {
        explicit spot_light_helper(const spot_light* light, float size = 1.0f);

    protected:
        void refresh() override;

    private:
        const spot_light* m_light;
        float m_size;

        // Last state the geometry was built from, so refresh() only
        // rebuilds when the light actually moves, turns or changes look.
        core::math::vec3 m_last_position{0.0f, 0.0f, 0.0f};
        core::math::vec3 m_last_direction{0.0f, 0.0f, 0.0f};
        core::math::vec3 m_last_color{0.0f, 0.0f, 0.0f};
        float m_last_outer_angle{0.0f};
        bool m_built{false};
    };
} // namespace rendering_engine::debug
