// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/editor/grid_helper.hpp>

#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/editor/grid_lines.hpp>

namespace rendering_engine::editor
{
    grid_helper::grid_helper(float size, int divisions, assets::color color, assets::color center_color)
        : line_helper("Grid")
    {
        namespace math = core::math;

        const float half = size * 0.5f;
        const math::vec3 line_rgb = to_rgb(color);
        const math::vec3 center_rgb = to_rgb(center_color);

        // The grid lies on the X/Y plane (z = 0) because the engine is
        // Z-up. grid_lines places the spaced lines and flags the one
        // through the origin (adding it when an odd division count puts
        // the origin mid-cell) so both the X- and Y-aligned spans through
        // the origin get the accent colour.
        const std::vector<grid_line> lines = grid_lines(size, divisions);

        std::vector<math::vec3> positions;
        std::vector<math::vec3> colors;
        positions.reserve(lines.size() * 4);
        colors.reserve(lines.size() * 4);

        for (const grid_line& line : lines)
        {
            const math::vec3& rgb = line.center ? center_rgb : line_rgb;

            // Span parallel to X at this Y.
            positions.push_back(math::vec3{-half, line.coord, 0.0f});
            positions.push_back(math::vec3{half, line.coord, 0.0f});
            colors.push_back(rgb);
            colors.push_back(rgb);

            // Span parallel to Y at this X.
            positions.push_back(math::vec3{line.coord, -half, 0.0f});
            positions.push_back(math::vec3{line.coord, half, 0.0f});
            colors.push_back(rgb);
            colors.push_back(rgb);
        }

        set_segments(positions, colors);
    }
} // namespace rendering_engine::editor
