// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file view_globals.hpp
 * @brief The per-view globals block (shaders/include/per_frame.glsl) the
 *        scene pass uploads once per frame, and the function that packs it.
 *
 * One buffer carries everything a scene shader knows about the view it
 * renders: the camera matrices and their inverses, the camera position,
 * the viewport, the frame clock, the temporal-AA jitter with the previous
 * frame's view-projection, and the scene-wide fog. The variable-size and
 * per-producer data (the lights, each shadow pass's matrices and maps)
 * stay in their own blocks of the same per-frame group.
 */

#pragma once

#include <cstddef>

#include <core/math/mat4.hpp>
#include <core/math/vec4.hpp>

namespace rendering_engine
{
    struct frame_context;

    // std140 mirror of the @c ViewGlobals block bound at slot 0, binding
    // @c shader_bindings::per_frame. Every member is a mat4 or a vec4, so
    // each lands on a 16-byte boundary with no padding and the C++ layout
    // matches the GLSL one byte-for-byte; the asserts below pin it. The
    // GLSL side documents the same table.
    struct view_globals
    {
        // World -> view, rigid (core::math::look_at).
        core::math::mat4 view;
        // View -> clip, GL convention, carrying this block's temporal-AA
        // jitter (see @ref jitter).
        core::math::mat4 projection;
        // projection * view.
        core::math::mat4 view_projection;
        core::math::mat4 inverse_view;
        core::math::mat4 inverse_projection;
        core::math::mat4 inverse_view_projection;
        // The previous frame's unjittered view-projection of the same
        // camera, or this frame's unjittered one when there is none (first
        // camera frame, camera switch), so a reprojection through it
        // yields zero motion rather than garbage.
        core::math::mat4 prev_view_projection;
        // xyz the camera's world position, w = 1.
        core::math::vec4 camera_position;
        // xy the viewport size in pixels, zw its reciprocal (0 for a zero
        // dimension).
        core::math::vec4 viewport;
        // x seconds since the engine clock started, y this frame's delta in
        // seconds; zw unused.
        core::math::vec4 time;
        // xy the NDC jitter baked into @ref projection, zw the previous
        // frame's. Zero while temporal AA is off and in the unjittered
        // overlay twin.
        core::math::vec4 jitter;
        // rgb the fog colour, a the mode (0 none, 1 linear, 2 exp2).
        core::math::vec4 fog_color;
        // x near, y far, z density, w height-fog density (0 disables it).
        core::math::vec4 fog_params;
        // x height-fog falloff, y reference height, z the view distance
        // the analytic height fog starts at (the volumetric fog's max
        // distance while it runs, else 0); w unused.
        core::math::vec4 height_fog_params;
    };

    static_assert(sizeof(view_globals) == 560, "ViewGlobals is seven std140 mat4s and seven vec4s");
    static_assert(offsetof(view_globals, view) == 0);
    static_assert(offsetof(view_globals, projection) == 64);
    static_assert(offsetof(view_globals, view_projection) == 128);
    static_assert(offsetof(view_globals, inverse_view) == 192);
    static_assert(offsetof(view_globals, inverse_projection) == 256);
    static_assert(offsetof(view_globals, inverse_view_projection) == 320);
    static_assert(offsetof(view_globals, prev_view_projection) == 384);
    static_assert(offsetof(view_globals, camera_position) == 448);
    static_assert(offsetof(view_globals, viewport) == 464);
    static_assert(offsetof(view_globals, time) == 480);
    static_assert(offsetof(view_globals, jitter) == 496);
    static_assert(offsetof(view_globals, fog_color) == 512);
    static_assert(offsetof(view_globals, fog_params) == 528);
    static_assert(offsetof(view_globals, height_fog_params) == 544);

    // The block for @p ctx's active camera, which must be non-null. With
    // @p apply_jitter the projection (and everything derived from it)
    // carries @ref frame_context::jitter and the jitter lanes report it;
    // without, the block describes the same view unjittered, as the scene
    // pass's overlay twin needs.
    view_globals make_view_globals(const frame_context& ctx, bool apply_jitter);
} // namespace rendering_engine
