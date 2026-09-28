// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/debug_draw/helper.hpp>
#include <rendering_engine/renderables/line.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine::debug_draw
{
    // A helper whose geometry is a list of independent line segments
    // (vertex pairs) drawn through its renderer's shared depth-disabled
    // debug line material, in the always-on-top overlay pass: it registers
    // itself with that renderer's debug-renderable registry on construction
    // and leaves it on destruction. The line-based gizmos (axes, bounding
    // box, light and camera wireframes) derive from this; they fill geometry
    // via @ref set_segments and optionally follow a moving target via
    // @ref refresh.
    struct line_helper : public helper, public renderable
    {
        line_helper(renderer& owner, const char* name);
        ~line_helper() override;

        // World placement of the gizmo. Helpers that bake their geometry
        // around the origin (axes) use it; helpers that bake world-space
        // geometry directly (box, light, camera) leave it at identity.
        core::transform transform;

        // Geometry is uploaded eagerly by @ref set_segments; nothing to do.
        void upload() final {}

        void collect_draw_items(std::vector<draw_item>& out) final;

    protected:
        // Replace the gizmo geometry with @p positions interpreted as
        // independent line segments (vertex pairs) and upload it. The two
        // vectors must be the same length.
        void set_segments(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Rebuild geometry from the helper's target before drawing.
        // Static helpers (axes) build once in their constructor and leave
        // this a no-op; helpers that follow a moving target (light,
        // camera) override it. Invoked from @ref collect_draw_items.
        virtual void refresh();

    private:
        line m_line;

        // The PerDraw block the pass pushes, built from @ref transform.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine::debug_draw
