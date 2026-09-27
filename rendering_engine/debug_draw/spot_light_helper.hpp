// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <core/math/math.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace rendering_engine
{
    struct spot_light;
}

namespace rendering_engine::debug_draw
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
} // namespace rendering_engine::debug_draw
