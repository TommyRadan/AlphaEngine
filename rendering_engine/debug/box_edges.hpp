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

#include <array>
#include <vector>

#include <infrastructure/math/vec3.hpp>

namespace rendering_engine::debug
{
    // Append the twelve edges of a hexahedron to @p positions /
    // @p colors as independent line segments, all tinted @p color.
    // Corners are indexed so bit 0 is the X axis, bit 1 the Y axis and
    // bit 2 the Z axis — the layout shared by @ref box_helper (an
    // axis-aligned box) and @ref camera_helper (the eight unprojected
    // frustum corners), so the edge table is written once here.
    inline void build_box_edges(const std::array<infrastructure::math::vec3, 8>& corners,
                                const infrastructure::math::vec3& color,
                                std::vector<infrastructure::math::vec3>& positions,
                                std::vector<infrastructure::math::vec3>& colors)
    {
        static constexpr std::array<std::array<int, 2>, 12> edges{
            {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

        positions.reserve(positions.size() + edges.size() * 2);
        colors.reserve(colors.size() + edges.size() * 2);
        for (const auto& edge : edges)
        {
            positions.push_back(corners[static_cast<size_t>(edge[0])]);
            positions.push_back(corners[static_cast<size_t>(edge[1])]);
            colors.push_back(color);
            colors.push_back(color);
        }
    }
} // namespace rendering_engine::debug
