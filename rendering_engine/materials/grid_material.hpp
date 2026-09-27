/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
