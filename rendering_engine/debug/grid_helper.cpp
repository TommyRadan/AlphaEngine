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

#include <rendering_engine/debug/grid_helper.hpp>

#include <vector>

#include <core/math/math.hpp>

namespace rendering_engine::debug
{
    grid_helper::grid_helper(float size, int divisions, util::color color, util::color center_color)
        : line_helper("Grid")
    {
        namespace math = core::math;

        if (divisions < 1)
        {
            divisions = 1;
        }

        const float half = size * 0.5f;
        const float step = size / static_cast<float>(divisions);
        const math::vec3 line_rgb = to_rgb(color);
        const math::vec3 center_rgb = to_rgb(center_color);

        std::vector<math::vec3> positions;
        std::vector<math::vec3> colors;
        positions.reserve(static_cast<size_t>(divisions + 1) * 4);
        colors.reserve(static_cast<size_t>(divisions + 1) * 4);

        // The grid lies on the X/Y plane (z = 0) because the engine is
        // Z-up. The middle index lands on the centre lines; flag it so
        // both the X- and Y-aligned spans through the origin get the
        // accent colour.
        const int center_index = divisions / 2;
        for (int i = 0; i <= divisions; ++i)
        {
            const float coord = -half + step * static_cast<float>(i);
            const math::vec3& rgb = (i == center_index) ? center_rgb : line_rgb;

            // Span parallel to X at this Y.
            positions.push_back(math::vec3{-half, coord, 0.0f});
            positions.push_back(math::vec3{half, coord, 0.0f});
            colors.push_back(rgb);
            colors.push_back(rgb);

            // Span parallel to Y at this X.
            positions.push_back(math::vec3{coord, -half, 0.0f});
            positions.push_back(math::vec3{coord, half, 0.0f});
            colors.push_back(rgb);
            colors.push_back(rgb);
        }

        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug
