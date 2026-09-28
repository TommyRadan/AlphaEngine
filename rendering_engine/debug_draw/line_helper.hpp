// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <vector>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/debug_draw/helper.hpp>
#include <rendering_engine/mesh_proxy.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/line.hpp>

namespace rendering_engine
{
    struct render_world;
}

namespace rendering_engine::debug_draw
{
    // A helper whose geometry is a list of independent line segments
    // (vertex pairs) drawn through its renderer's shared depth-disabled
    // debug line material, in the always-on-top overlay pass: it keeps an
    // overlay mesh proxy (@ref mesh_description::overlay) in that
    // renderer's world from construction to destruction. The line-based
    // gizmos (axes, bounding box, light and camera wireframes) derive from
    // this; they fill geometry via @ref set_segments and optionally follow a
    // moving target via @ref refresh.
    struct line_helper : public helper
    {
        line_helper(renderer& owner, const char* name);
        ~line_helper() override;

        // World placement of the gizmo. Helpers that bake their geometry
        // around the origin (axes) use it; helpers that bake world-space
        // geometry directly (box, light, camera) leave it at identity.
        // @ref update hands it to the proxy.
        core::transform transform;

        // Shows or hides the gizmo's proxy.
        void set_visible(bool visible) override;

        // While visible: lets the gizmo follow its target (@ref refresh)
        // and places the proxy at @ref transform when it moved.
        void update() override;

    protected:
        // Replace the gizmo geometry with @p positions interpreted as
        // independent line segments (vertex pairs), upload it and hand it
        // to the proxy. The two vectors must be the same length.
        void set_segments(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Rebuild geometry from the helper's target. Static helpers (axes)
        // build once in their constructor and leave this a no-op; helpers
        // that follow a moving target (light, camera, physics) override
        // it. Invoked from @ref update.
        virtual void refresh();

    private:
        // What the proxy draws: the line's geometry, on the overlay.
        mesh_description describe() const;

        line m_line;

        // The overlay proxy in the owner's world, and the transform stamp
        // it was last placed from.
        render_world* m_world{nullptr};
        mesh_proxy_handle m_proxy{};
        uint64_t m_placed_version{0};
    };
} // namespace rendering_engine::debug_draw
