// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <core/math/aabb.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    // Bit 0: the layer every renderable is on by default (@ref
    // renderable::layer_mask). A pass or camera that does not care about
    // layers filters with @ref layer_all, which includes this bit, so
    // nothing is excluded until a caller narrows either side.
    constexpr uint32_t layer_default = 1u << 0;

    // Editor-only geometry: the debug helpers registered as scene
    // renderables (e.g. the infinite ground grid) carry this bit instead
    // of @ref layer_default. It is included in @ref layer_all, so a fresh
    // camera still renders it and nothing changes visually by default; a
    // game builds a camera whose @ref camera::set_culling_mask clears this
    // bit to hide editor gizmos from gameplay views while an editor
    // viewport (the default mask) keeps seeing them.
    constexpr uint32_t layer_editor = 1u << 31;

    // Every layer bit set: the default @ref camera::culling_mask and the
    // default shadow-pass caster mask, so nothing is excluded by layer
    // until a caller narrows one of them.
    constexpr uint32_t layer_all = ~0u;

    // Anything that can contribute draws to a render pass. @ref upload
    // allocates GPU resources (buffers, bind groups) once GL is alive;
    // @ref collect_draw_items appends one or more @ref draw_item values
    // describing what to draw this frame. The pass sorts the collected
    // items by material and dispatches them in one place — renderables
    // never call @c set_pipeline / @c set_bind_group / @c draw_indexed
    // themselves.
    //
    // The output vector is provided by the caller so the pass can reuse
    // a single allocation across frames; composite renderables (e.g. a
    // @c sprite_batch over several textures) push N items into the same
    // vector.
    struct renderable
    {
        virtual ~renderable() = default;
        virtual void upload() = 0;
        virtual void collect_draw_items(std::vector<draw_item>& out) = 0;

        // Layer bits this renderable belongs to (see @ref layer_default /
        // @ref layer_editor / @ref layer_all). A pass or camera that
        // filters by layer skips it whenever
        // @c (layer_mask & filter_mask) == 0, the same way a frustum cull
        // does; the default puts every renderable on the default layer,
        // which every default filter mask includes.
        uint32_t layer_mask{layer_default};

        // Whether this renderable contributes occluders to the shadow
        // passes. Defaults to true; non-physical geometry (debug
        // helpers, fullscreen-triangle effects) overrides it to false so
        // the depth-only shadow pipeline never rasterizes its clip-space
        // or gizmo vertices into the shadow map as a phantom caster.
        virtual bool casts_shadow() const
        {
            return true;
        }

        // World-space axis-aligned bounds of everything this renderable
        // would draw this frame, for frustum culling. Returns true and
        // fills @p out when the geometry has a finite extent (a mesh under
        // its world transform, an instanced batch, a line strip); returns
        // false — leaving @p out untouched — when it does not (clip-space
        // fullscreen effects, debug gizmos, UI), and such renderables are
        // never culled. The passes call this before @ref collect_draw_items
        // and skip the collect for a culled renderable, so the answer must
        // not depend on the collect having run.
        virtual bool world_bounds(core::math::aabb& out) const
        {
            (void)out;
            return false;
        }

        // The same geometry's axis-aligned bounds in the space of the
        // renderable's parent: its own local transform applied, not its
        // parent chain's. For a renderable a component hangs under a node
        // that is the node's local space, which is what the physics
        // colliders fit themselves to. Returns false, leaving @p out
        // untouched, where there is no such box (the default).
        virtual bool local_bounds(core::math::aabb& out) const
        {
            (void)out;
            return false;
        }
    };
} // namespace rendering_engine
