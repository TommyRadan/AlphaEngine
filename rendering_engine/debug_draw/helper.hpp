// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct renderer;
}

namespace rendering_engine::debug_draw
{
    // Which pass a helper draws in.
    enum class helper_layer
    {
        // The depth-less debug-overlay pass: always-on-top gizmos that
        // never get occluded (axes, light/camera/box wireframes). Drawn
        // only in debug builds.
        overlay,

        // The 3D scene pass: depth-tested geometry that integrates with
        // the scene and is occluded by it (the infinite ground grid).
        scene,
    };

    // Base for the debug gizmo family of @c *Helper objects.
    // A helper carries a display name and a
    // visibility flag, and on construction registers itself twice, both
    // with the @ref renderer it is constructed with: into its
    // @ref render_world's helper list the debug UI walks to toggle
    // visibility, and into one of its renderable registries (chosen by
    // @ref helper_layer) so the matching pass draws it. Destroying it
    // unregisters from both, so it must go before that renderer's
    // @ref renderer::quit. Every
    // helper's @ref renderable::layer_mask is set to @ref layer_editor, so
    // a scene-layer helper (@ref infinite_grid) can be hidden from a
    // gameplay camera by clearing that bit from its culling mask, without
    // affecting anything by default.
    //
    // The geometry itself lives in the subclasses: @ref line_helper for
    // the line-based overlay gizmos, @ref infinite_grid for the
    // shader-driven ground grid. Like the rest of the debug-overlay
    // machinery the types are always compiled, but the overlay layer is
    // inert in release where the debug pass is dropped.
    struct helper : public renderable
    {
        helper(renderer& owner, const char* name, helper_layer layer);
        ~helper() override;

        helper(const helper&) = delete;
        helper& operator=(const helper&) = delete;

        // Human-readable label shown in the debug UI helper panel.
        const char* name() const noexcept;

        // When false the helper contributes no draws this frame. The
        // debug UI flips this per helper; defaults to visible.
        bool visible{true};

        // Debug gizmos are never shadow casters: the scene-layer grid is
        // a clip-space fullscreen triangle and the overlay gizmos are
        // thin lines, so feeding either to the depth-only shadow pipeline
        // writes garbage occluders into the shadow map.
        bool casts_shadow() const override
        {
            return false;
        }

    protected:
        // Linear-RGB triple in [0, 1] from a 0..255 @ref color,
        // ignoring alpha.
        static core::math::vec3 to_rgb(const assets::color& c);

    private:
        renderer* m_renderer;
        const char* m_name;
        helper_layer m_layer;
    };
} // namespace rendering_engine::debug_draw
