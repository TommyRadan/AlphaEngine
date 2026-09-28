// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace rendering_engine
{
    struct directional_light;
}

namespace rendering_engine::debug_draw
{
    // Gizmo for a directional light. Draws a small square facing the
    // light's travel direction at the world origin plus a ray along that
    // direction, both tinted with the light's colour. The geometry tracks
    // the light's direction / colour every frame, so the helper must not
    // outlive the light it points at.
    struct directional_light_helper : public line_helper
    {
        directional_light_helper(renderer& owner, const directional_light* light, float size = 1.0f);

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
} // namespace rendering_engine::debug_draw
