// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file rect_transform.hpp
 * @brief Anchored / stretched UI rects in pixel space and hit-testing them.
 *
 * UI space is the drawable in pixels: (0, 0) at the top-left corner, +x
 * right, +y down — the space the ui pass's projection maps and the
 * @ref sprite_batch draws in. Mouse events report window coordinates
 * (logical points), so a cursor goes through @ref window_to_pixels before
 * it is tested against a rect.
 */

#pragma once

#include <core/math/vec2.hpp>

namespace platform
{
    struct window;
}

namespace rendering_engine
{
    /** @brief An axis-aligned rect in UI pixels. */
    struct ui_rect
    {
        core::math::vec2 min{0.0f, 0.0f};
        core::math::vec2 max{0.0f, 0.0f};

        core::math::vec2 size() const;

        /**
         * @brief Whether @p point lies inside: the min edges are inclusive
         *        and the max edges exclusive, so two rects sharing an edge
         *        never both claim a point.
         */
        bool contains(const core::math::vec2& point) const;
    };

    /** @brief Named anchor and pivot points, as fractions of a rect. */
    namespace ui_anchor
    {
        inline constexpr core::math::vec2 top_left{0.0f, 0.0f};
        inline constexpr core::math::vec2 top{0.5f, 0.0f};
        inline constexpr core::math::vec2 top_right{1.0f, 0.0f};
        inline constexpr core::math::vec2 left{0.0f, 0.5f};
        inline constexpr core::math::vec2 center{0.5f, 0.5f};
        inline constexpr core::math::vec2 right{1.0f, 0.5f};
        inline constexpr core::math::vec2 bottom_left{0.0f, 1.0f};
        inline constexpr core::math::vec2 bottom{0.5f, 1.0f};
        inline constexpr core::math::vec2 bottom_right{1.0f, 1.0f};
    } // namespace ui_anchor

    /**
     * @brief Where a UI element sits in its parent rect: anchors, a pivot,
     *        an offset and a size, after Unity's @c RectTransform.
     *
     * The anchors are fractions of the parent. Equal anchors on an axis pin
     * the element to that point and @ref size is its pixel extent there;
     * differing anchors stretch it with the parent between them and
     * @ref size is added to that span (negative to inset). The @ref pivot
     * is the point of the element, a fraction of its own size, that lands
     * @ref position pixels from the anchors' reference point (the anchors
     * lerped by the pivot) and that @ref rotation turns about.
     *
     * Every resolved coordinate is affine in the parent's size, which is
     * what lets @ref sprite_batch hand the anchors to the vertex shader
     * instead of baking pixels: a top-level element follows the drawable
     * through a resize without being rebuilt.
     */
    struct rect_transform
    {
        core::math::vec2 anchor_min{0.0f, 0.0f};
        core::math::vec2 anchor_max{0.0f, 0.0f};
        core::math::vec2 pivot{0.0f, 0.0f};
        core::math::vec2 position{0.0f, 0.0f};
        core::math::vec2 size{0.0f, 0.0f};

        /** @brief Radians about the pivot; positive turns clockwise on screen. */
        float rotation{0.0f};

        /**
         * @brief An element pinned to @p anchor in its parent, @p size
         *        pixels large, whose @p pivot sits @p position pixels from
         *        the anchor.
         */
        static rect_transform anchored(const core::math::vec2& anchor,
                                       const core::math::vec2& pivot,
                                       const core::math::vec2& position,
                                       const core::math::vec2& size);

        /**
         * @brief An element filling its parent between @p anchor_min and
         *        @p anchor_max, its edges moved in by @p inset_min pixels
         *        at the min side and @p inset_max at the max side. The
         *        pivot is the centre.
         *
         * Meant for anchors that differ on both axes: an axis whose two
         * anchors are equal has no span for the insets to eat into. For
         * an element that stretches along one axis only, fill the fields
         * in directly.
         */
        static rect_transform stretched(const core::math::vec2& anchor_min,
                                        const core::math::vec2& anchor_max,
                                        const core::math::vec2& inset_min = core::math::vec2{0.0f, 0.0f},
                                        const core::math::vec2& inset_max = core::math::vec2{0.0f, 0.0f});

        /** @brief The element's pixel size inside @p parent. */
        core::math::vec2 resolved_size(const ui_rect& parent) const;

        /** @brief Where the pivot lands inside @p parent, in pixels. */
        core::math::vec2 resolved_pivot(const ui_rect& parent) const;

        /** @brief The element's rect inside @p parent, before @ref rotation. */
        ui_rect resolve(const ui_rect& parent) const;

        /**
         * @brief Hit test: whether @p point (pixels, in @p parent's space)
         *        lies on the element, @ref rotation included. Edges follow
         *        @ref ui_rect::contains.
         */
        bool contains(const ui_rect& parent, const core::math::vec2& point) const;
    };

    bool operator==(const rect_transform& a, const rect_transform& b);
    bool operator!=(const rect_transform& a, const rect_transform& b);

    /**
     * @brief Converts @p point from window coordinates (logical points,
     *        the space of the mouse events' @c m_x / @c m_y) of a window
     *        @p window_size points large to UI pixels on a drawable
     *        @p pixel_size pixels large. A degenerate window maps nothing
     *        and returns @p point unchanged.
     */
    core::math::vec2 window_to_pixels(const core::math::vec2& point,
                                      const core::math::vec2& window_size,
                                      const core::math::vec2& pixel_size);

    /**
     * @brief @ref window_to_pixels against @p window's live size: turns a
     *        mouse event's position into the UI pixel it is over. Main
     *        thread, while the window is up.
     */
    core::math::vec2 window_to_pixels(const platform::window& window, const core::math::vec2& point);

    /**
     * @brief @p window's drawable as a UI rect, (0, 0) to its pixel size:
     *        the parent of every top-level element. Main thread, while the
     *        window is up.
     */
    ui_rect drawable_rect(const platform::window& window);
} // namespace rendering_engine
