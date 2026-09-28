// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <core/math/math.hpp>

namespace rendering_engine
{
    struct render_world;
    struct renderer;
} // namespace rendering_engine

namespace rendering_engine::debug_draw
{
    // Base for the debug gizmo family of @c *Helper objects.
    // A helper carries a display name and a visibility flag, and on
    // construction adds itself to the helper list of the @ref renderer it is
    // constructed with (its @ref render_world's, which the debug UI walks to
    // toggle visibility); destroying it removes it, so it must go before
    // that renderer's @ref renderer::quit.
    //
    // The geometry itself lives in the subclasses, each drawn through a
    // mesh proxy in that world: @ref line_helper for the line-based gizmos
    // the debug pass draws on top of everything, and @ref infinite_grid
    // for the shader-driven ground grid the scene pass draws. Every helper
    // is editor-only geometry on @ref layer_editor, so a gameplay camera
    // can hide it by clearing that bit from its culling mask, and none
    // casts a shadow. A helper that follows something rebuilds its
    // geometry in @ref update, which @ref update_helpers calls once per
    // frame, after the render extraction and before the frame is drawn.
    // Like the rest of the debug-overlay machinery the types are always
    // compiled, but the overlay is inert in release where the debug pass
    // is dropped.
    struct helper
    {
        helper(renderer& owner, const char* name);
        virtual ~helper();

        helper(const helper&) = delete;
        helper& operator=(const helper&) = delete;

        // Human-readable label shown in the debug UI helper panel.
        const char* name() const noexcept;

        // Whether the helper draws; the debug UI flips it per helper.
        // Defaults to visible.
        bool is_visible() const noexcept;
        virtual void set_visible(bool visible);

        // Brings the helper's proxy up to date with what it follows (a
        // light, a camera, the physics world) and with its placement. Does
        // nothing by default, or while the helper is hidden.
        virtual void update();

    protected:
        // Linear-RGB triple in [0, 1] from a 0..255 @ref color,
        // ignoring alpha.
        static core::math::vec3 to_rgb(const assets::color& c);

        // The renderer the helper was constructed with.
        renderer& owner() const noexcept;

        // The world of the renderer the helper was constructed with, whose
        // proxies the light and camera gizmos follow.
        const render_world& renderer_world() const noexcept;

    private:
        renderer* m_renderer;
        const char* m_name;
        bool m_visible{true};
    };

    // Calls @ref helper::update on every helper in @p world's helper list,
    // in construction order. @c runtime::engine::tick calls it once per
    // frame, right after the render extraction, so a gizmo shows the
    // proxies it follows as this frame draws them.
    void update_helpers(const render_world& world);
} // namespace rendering_engine::debug_draw
