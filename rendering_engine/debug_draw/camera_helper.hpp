// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace rendering_engine
{
    struct camera;
}

namespace rendering_engine::debug_draw
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
        camera_helper(renderer& owner, const camera* cam, assets::color color = assets::color{200, 200, 80, 255});

    protected:
        void refresh() override;

    private:
        const camera* m_camera;
        assets::color m_color;

        // Last view-projection the geometry was built from, so refresh()
        // only rebuilds when the camera moves.
        core::math::mat4 m_last_view_projection{};
        bool m_built{false};
    };
} // namespace rendering_engine::debug_draw
