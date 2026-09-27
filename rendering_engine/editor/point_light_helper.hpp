// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/editor/line_helper.hpp>

namespace rendering_engine
{
    struct point_light;
}

namespace rendering_engine::editor
{
    // Gizmo for a point light.
    // Draws a small octahedron wireframe at the light's world position,
    // tinted with the light's colour. The geometry tracks the light's
    // position / colour every frame, so the helper must not outlive the
    // light it points at.
    struct point_light_helper : public line_helper
    {
        explicit point_light_helper(const point_light* light, float size = 0.25f);

    protected:
        void refresh() override;

    private:
        const point_light* m_light;
        float m_size;

        // Last state the geometry was built from, so refresh() only
        // rebuilds when the light actually moves or changes colour.
        core::math::vec3 m_last_position{0.0f, 0.0f, 0.0f};
        core::math::vec3 m_last_color{0.0f, 0.0f, 0.0f};
        bool m_built{false};
    };
} // namespace rendering_engine::editor
