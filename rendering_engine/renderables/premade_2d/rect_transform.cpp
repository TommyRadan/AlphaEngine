// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_2d/rect_transform.hpp>

#include <cmath>

#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    core::math::vec2 ui_rect::size() const
    {
        return max - min;
    }

    bool ui_rect::contains(const core::math::vec2& point) const
    {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }

    rect_transform rect_transform::anchored(const core::math::vec2& anchor,
                                            const core::math::vec2& pivot,
                                            const core::math::vec2& position,
                                            const core::math::vec2& size)
    {
        rect_transform rect{};
        rect.anchor_min = anchor;
        rect.anchor_max = anchor;
        rect.pivot = pivot;
        rect.position = position;
        rect.size = size;
        return rect;
    }

    rect_transform rect_transform::stretched(const core::math::vec2& anchor_min,
                                             const core::math::vec2& anchor_max,
                                             const core::math::vec2& inset_min,
                                             const core::math::vec2& inset_max)
    {
        rect_transform rect{};
        rect.anchor_min = anchor_min;
        rect.anchor_max = anchor_max;
        rect.pivot = ui_anchor::center;
        // The span shrinks by both insets, and the centred pivot moves by
        // half their difference to keep each edge where it was asked for.
        rect.size = -(inset_min + inset_max);
        rect.position = (inset_min - inset_max) * 0.5f;
        return rect;
    }

    core::math::vec2 rect_transform::resolved_size(const ui_rect& parent) const
    {
        return (anchor_max - anchor_min) * parent.size() + size;
    }

    core::math::vec2 rect_transform::resolved_pivot(const ui_rect& parent) const
    {
        const core::math::vec2 reference = anchor_min + (anchor_max - anchor_min) * pivot;
        return parent.min + reference * parent.size() + position;
    }

    ui_rect rect_transform::resolve(const ui_rect& parent) const
    {
        const core::math::vec2 extent = resolved_size(parent);
        ui_rect rect{};
        rect.min = resolved_pivot(parent) - pivot * extent;
        rect.max = rect.min + extent;
        return rect;
    }

    bool rect_transform::contains(const ui_rect& parent, const core::math::vec2& point) const
    {
        // Undo the rotation about the pivot, then test the upright rect
        // in pivot-relative pixels.
        const core::math::vec2 relative = point - resolved_pivot(parent);
        const float c = std::cos(rotation);
        const float s = std::sin(rotation);
        const core::math::vec2 upright{c * relative.x + s * relative.y, -s * relative.x + c * relative.y};

        const core::math::vec2 extent = resolved_size(parent);
        ui_rect local{};
        local.min = -(pivot * extent);
        local.max = local.min + extent;
        return local.contains(upright);
    }

    bool operator==(const rect_transform& a, const rect_transform& b)
    {
        return a.anchor_min == b.anchor_min && a.anchor_max == b.anchor_max && a.pivot == b.pivot &&
               a.position == b.position && a.size == b.size && a.rotation == b.rotation;
    }

    bool operator!=(const rect_transform& a, const rect_transform& b)
    {
        return !(a == b);
    }

    core::math::vec2 window_to_pixels(const core::math::vec2& point,
                                      const core::math::vec2& window_size,
                                      const core::math::vec2& pixel_size)
    {
        if (window_size.x <= 0.0f || window_size.y <= 0.0f)
        {
            return point;
        }
        return point * (pixel_size / window_size);
    }

    core::math::vec2 window_to_pixels(const core::math::vec2& point)
    {
        const window& win = *runtime::current_engine().window;
        const window_extent logical = win.size();
        const window_extent pixels = win.pixel_size();
        return window_to_pixels(point,
                                core::math::vec2{static_cast<float>(logical.width), static_cast<float>(logical.height)},
                                core::math::vec2{static_cast<float>(pixels.width), static_cast<float>(pixels.height)});
    }

    ui_rect drawable_rect()
    {
        const window_extent pixels = runtime::current_engine().window->pixel_size();
        ui_rect rect{};
        rect.max = core::math::vec2{static_cast<float>(pixels.width), static_cast<float>(pixels.height)};
        return rect;
    }
} // namespace rendering_engine
