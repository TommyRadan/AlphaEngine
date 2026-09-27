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
    // drawn by a single fullscreen triangle (see @ref editor::infinite_grid)
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
        // @p tmpl is the grid template (see @ref create_template).
        explicit grid_material(std::shared_ptr<material_template> tmpl);
        ~grid_material() override;

        // The grid template. @p frame_layout is the scene_pass per-frame
        // layout bound at slot 0. @p fade_distance is the world-space
        // radius (from the camera) past which the grid has fully faded to
        // nothing; it is baked into the template's shaders as the
        // @c GRID_FADE_DISTANCE define.
        static std::shared_ptr<material_template>
        create_template(gpu::device& device, gpu::bind_group_layout frame_layout, float fade_distance = 100.0f);
    };
} // namespace rendering_engine
