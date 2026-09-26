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

/**
 * @file grid_lines.hpp
 * @brief Line layout of the finite @ref grid_helper, kept device-free so
 *        the centre-line placement is unit-testable.
 */

#pragma once

#include <vector>

namespace rendering_engine::debug
{
    // One line of the finite grid along one axis: where it sits (the
    // coordinate along the perpendicular axis) and whether it is the
    // accented centre line through the origin.
    struct grid_line
    {
        float coord{0.0f};
        bool center{false};
    };

    // The lines a @p size x @p size grid with @p divisions cells per side
    // draws along each axis, in ascending coordinate order. There are
    // @c divisions + 1 evenly spaced lines; the one at coordinate 0 is the
    // centre line. An odd division count places the origin in the middle
    // of a cell, so no spaced line passes through it: an extra centre line
    // at 0 is inserted then, so the origin always reads at a glance.
    // @p divisions below 1 is treated as 1.
    inline std::vector<grid_line> grid_lines(float size, int divisions)
    {
        if (divisions < 1)
        {
            divisions = 1;
        }

        const float half = size * 0.5f;
        const float step = size / static_cast<float>(divisions);
        const bool even = (divisions % 2) == 0;

        std::vector<grid_line> lines;
        lines.reserve(static_cast<size_t>(divisions) + (even ? 1u : 2u));
        for (int i = 0; i <= divisions; ++i)
        {
            // An even count puts line divisions / 2 exactly on the origin;
            // computing the coordinate from the index (rather than testing
            // the float against 0) keeps that exact.
            const bool center = even && i * 2 == divisions;
            const float coord = center ? 0.0f : -half + step * static_cast<float>(i);
            if (!even && i * 2 == divisions + 1)
            {
                // First spaced line past the origin: slot the centre line
                // in ahead of it so the order stays ascending.
                lines.push_back(grid_line{0.0f, true});
            }
            lines.push_back(grid_line{coord, center});
        }
        return lines;
    }
} // namespace rendering_engine::debug
