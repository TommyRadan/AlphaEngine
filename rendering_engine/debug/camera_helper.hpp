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
#include <rendering_engine/util/color.hpp>

namespace rendering_engine
{
    struct camera;
}

namespace rendering_engine::debug
{
    // Wireframe of a camera's view frustum.
    // The eight clip-space corners are unprojected
    // through the inverse view-projection into world space and drawn as a
    // hexahedron, so the gizmo shows exactly what the camera sees. The
    // geometry tracks the camera's view-projection every frame, so the
    // helper must not outlive the camera it points at.
    //
    // Visualising the active camera's own frustum is degenerate (it fills
    // the screen); this is meant for a secondary / inactive camera.
    struct camera_helper : public line_helper
    {
        explicit camera_helper(const camera* cam, util::color color = util::color{200, 200, 80, 255});

    protected:
        void refresh() override;

    private:
        const camera* m_camera;
        util::color m_color;

        // Last view-projection the geometry was built from, so refresh()
        // only rebuilds when the camera moves.
        core::math::mat4 m_last_view_projection{};
        bool m_built{false};
    };
} // namespace rendering_engine::debug
