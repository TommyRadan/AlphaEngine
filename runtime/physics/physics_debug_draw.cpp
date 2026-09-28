// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/physics/physics_debug_draw.hpp>

#include <cstddef>
#include <vector>

#include <core/math/vec3.hpp>
#include <rendering_engine/debug_draw/debug_draw.hpp>
#include <runtime/physics/physics_world.hpp>

namespace runtime::physics
{
    void draw_debug(const world& physics, rendering_engine::render_world& target)
    {
        if constexpr (rendering_engine::debug_draw::enabled)
        {
            std::vector<core::math::vec3> positions;
            std::vector<core::math::vec3> colors;
            physics.debug_lines(positions, colors);
            for (std::size_t i = 0; i + 1 < positions.size(); i += 2)
            {
                rendering_engine::debug_draw::line(target, positions[i], positions[i + 1], colors[i]);
            }
        }
    }
} // namespace runtime::physics
