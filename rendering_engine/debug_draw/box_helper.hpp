// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <assets/color.hpp>
#include <core/math/aabb.hpp>
#include <rendering_engine/debug_draw/line_helper.hpp>

namespace rendering_engine::debug_draw
{
    // Wireframe of an axis-aligned bounding box.
    // The twelve edges are baked in world space, so
    // the inherited @ref transform is left at identity; call
    // @ref set_box to follow a box that moves.
    struct box_helper : public line_helper
    {
        explicit box_helper(renderer& owner,
                            const core::math::aabb& box = core::math::aabb{},
                            assets::color color = assets::color{255, 255, 0, 255});

        // Replace the box and rebuild the wireframe.
        void set_box(const core::math::aabb& box);

    private:
        void rebuild();

        core::math::aabb m_box;
        assets::color m_color;
    };
} // namespace rendering_engine::debug_draw
