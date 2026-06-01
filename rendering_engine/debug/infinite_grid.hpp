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

#include <rendering_engine/debug/helper.hpp>
#include <rendering_engine/gpu/handle.hpp>

namespace rendering_engine
{
    struct grid_material;
}

namespace rendering_engine::debug
{
    // The CAD-style infinite ground grid — an unbounded analogue of
    // @c THREE.GridHelper. Unlike the line-based @ref grid_helper it is a
    // scene-pass renderable: a single fullscreen triangle fronts the
    // analytic @ref grid_material, whose fragment shader reconstructs the
    // ground plane (z = 0, the engine is Z-up), draws minor / major lines
    // plus the coloured world axes, fades with distance and depth-tests
    // against the scene, so it stretches to the horizon and is occluded by
    // scene geometry.
    //
    // Created as a built-in gizmo in debug builds and toggled from the
    // debug UI like the other helpers.
    struct infinite_grid : public helper
    {
        // @p fade_distance is the world-space radius past which the grid
        // has fully faded.
        explicit infinite_grid(float fade_distance = 100.0f);
        ~infinite_grid() override;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

    private:
        grid_material* m_material{nullptr};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine::debug
