// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>
#include <rendering_engine/render_proxies.hpp>

namespace rendering_engine::debug_draw
{
    // Gizmo for a point light.
    // Draws a small octahedron wireframe at the light's world position,
    // tinted with the light's colour. The geometry tracks the position /
    // colour of the light proxy it names in its renderer's world every
    // frame, and draws nothing once that proxy is gone.
    struct point_light_helper : public line_helper
    {
        point_light_helper(renderer& owner, light_proxy_handle light, float size = 0.25f);

    protected:
        void refresh() override;

    private:
        light_proxy_handle m_light;
        float m_size;

        // Last state the geometry was built from, so refresh() only
        // rebuilds when the light actually moves or changes colour.
        core::math::vec3 m_last_position{0.0f, 0.0f, 0.0f};
        core::math::vec3 m_last_color{0.0f, 0.0f, 0.0f};
        bool m_built{false};
    };
} // namespace rendering_engine::debug_draw
