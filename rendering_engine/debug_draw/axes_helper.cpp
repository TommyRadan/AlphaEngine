// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/axes_helper.hpp>

#include <vector>

#include <core/math/math.hpp>

namespace rendering_engine::debug_draw
{
    axes_helper::axes_helper(float size) : line_helper("Axes")
    {
        namespace math = core::math;

        const math::vec3 origin{0.0f, 0.0f, 0.0f};
        const math::vec3 red{1.0f, 0.0f, 0.0f};
        const math::vec3 green{0.0f, 1.0f, 0.0f};
        const math::vec3 blue{0.0f, 0.0f, 1.0f};

        const std::vector<math::vec3> positions{origin,
                                                math::vec3{size, 0.0f, 0.0f},
                                                origin,
                                                math::vec3{0.0f, size, 0.0f},
                                                origin,
                                                math::vec3{0.0f, 0.0f, size}};
        const std::vector<math::vec3> colors{red, red, green, green, blue, blue};

        set_segments(positions, colors);
    }
} // namespace rendering_engine::debug_draw
