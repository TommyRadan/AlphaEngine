// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine::debug_draw
{
    // Wireframe of a camera's view frustum.
    // The eight clip-space corners are unprojected
    // through the inverse view-projection into world space and drawn as a
    // hexahedron, so the gizmo shows exactly what the camera sees. The
    // geometry tracks the view-projection of the camera proxy it names in
    // its renderer's world every frame, and draws nothing once that proxy
    // is gone.
    //
    // Visualising the active camera's own frustum is degenerate (it fills
    // the screen); this is meant for a secondary / inactive camera.
    struct camera_helper : public line_helper
    {
        camera_helper(renderer& owner, camera_proxy_handle cam, assets::color color = assets::color{200, 200, 80, 255});

    protected:
        void refresh() override;

    private:
        camera_proxy_handle m_camera;
        assets::color m_color;

        // Last view-projection the geometry was built from, so refresh()
        // only rebuilds when the camera moves.
        core::math::mat4 m_last_view_projection{};
        bool m_built{false};
    };
} // namespace rendering_engine::debug_draw
