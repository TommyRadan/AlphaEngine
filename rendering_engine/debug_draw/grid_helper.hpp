// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace rendering_engine::debug_draw
{
    // A finite square reference grid on the X/Y ground plane (the engine
    // is Z-up) centred at the origin — a bounded line-based grid.
    // The line through the centre on each axis is
    // drawn in @p center_color so the origin reads at a glance; the
    // remaining lines use @p color. For an unbounded grid that integrates
    // with the scene depth, see @ref infinite_grid.
    struct grid_helper : public line_helper
    {
        // @p size is the full edge length of the grid, @p divisions the
        // number of cells per side. An odd count puts the origin in the
        // middle of a cell, so a centre line is added on top of the
        // spaced ones (see @ref grid_lines). Geometry is baked once at
        // construction.
        explicit grid_helper(float size = 10.0f,
                             int divisions = 10,
                             assets::color color = assets::color{120, 120, 120, 255},
                             assets::color center_color = assets::color{70, 70, 70, 255});
    };
} // namespace rendering_engine::debug_draw
