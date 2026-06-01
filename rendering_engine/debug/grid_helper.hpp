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

#include <rendering_engine/debug/line_helper.hpp>
#include <rendering_engine/util/color.hpp>

namespace rendering_engine::debug
{
    // A finite square reference grid on the X/Y ground plane (the engine
    // is Z-up) centred at the origin — a bounded line-based analogue of
    // @c THREE.GridHelper. The line through the centre on each axis is
    // drawn in @p center_color so the origin reads at a glance; the
    // remaining lines use @p color. For an unbounded grid that integrates
    // with the scene depth, see @ref infinite_grid.
    struct grid_helper : public line_helper
    {
        // @p size is the full edge length of the grid, @p divisions the
        // number of cells per side. Geometry is baked once at
        // construction.
        explicit grid_helper(float size = 10.0f,
                             int divisions = 10,
                             util::color color = util::color{120, 120, 120, 255},
                             util::color center_color = util::color{70, 70, 70, 255});
    };
} // namespace rendering_engine::debug
