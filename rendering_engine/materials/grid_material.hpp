// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>

namespace rendering_engine
{
    // Analytic infinite-grid material — the CAD-style ground grid. It is
    // drawn by a single fullscreen triangle (see @ref debug_draw::infinite_grid)
    // whose fragment shader reconstructs, per pixel, the world point where
    // the view ray crosses the Z-up ground plane (z = 0), draws minor /
    // major grid lines and the coloured world axes with screen-space
    // derivative anti-aliasing, and fades the whole thing out with
    // distance. It writes per-pixel depth and depth-tests against the
    // scene (depth-write off) so scene geometry occludes the grid.
    //
    // Slot layout mirrors the line material: the scene per-frame group
    // (camera) at slot 0, and a per-draw group at slot 1 holding a model
    // matrix that places / orients the ground plane (identity for the
    // origin grid). There is no per-material group.
    struct grid_material : public material
    {
        // @p tmpl is a grid template (see @ref describe).
        explicit grid_material(std::shared_ptr<material_template> tmpl);
        ~grid_material() override;

        // The descriptor of a grid template over the scene pass's
        // per-frame set. @p fade_distance is the world-space radius (from
        // the camera) past which the grid has fully faded to nothing; it
        // is baked into the template's shaders as the
        // @c GRID_FADE_DISTANCE define, so each distance is a template of
        // its own. The material library registers the default one as
        // "grid" (material_library::create_template) and builds the others
        // unregistered (material_library::create_grid_material). Touches no
        // gpu::device.
        static material_template_descriptor describe(float fade_distance = 100.0f);
    };
} // namespace rendering_engine
