// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file draw_list.hpp
 * @brief The debug-draw list: the line segments and text labels recorded
 *        through the immediate-mode debug-draw API, kept until they expire.
 */

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/math/vec3.hpp>

namespace rendering_engine::debug_draw
{
    /** @brief One recorded line segment, in world space. */
    struct line_segment
    {
        core::math::vec3 from{};
        core::math::vec3 to{};
        // Linear RGB in [0, 1].
        core::math::vec3 color{1.0f, 1.0f, 1.0f};
        // Seconds of world time left before it expires (see draw_list::advance).
        float remaining{0.0f};
    };

    /** @brief One recorded text label, anchored at a world-space point. */
    struct text_label
    {
        core::math::vec3 position{};
        std::string text;
        // Linear RGB in [0, 1].
        core::math::vec3 color{1.0f, 1.0f, 1.0f};
        // Seconds of world time left before it expires (see draw_list::advance).
        float remaining{0.0f};
    };

    /**
     * @brief What the debug-draw functions (debug_draw.hpp) have recorded
     *        and not yet expired.
     *
     * A @ref render_world owns one (@c render_world::debug_draw_list). Every
     * entry is drawn in the frame it was recorded in, and again in every
     * later frame until its duration has run out: @ref advance, which the
     * engine calls once per tick with the tick's world-time delta before
     * anything records, ages the entries and drops the expired ones, so an
     * entry recorded with a zero duration is drawn exactly once. The
     * renderer turns the segments into its debug line proxies between frames
     * and the editor draws the labels; nothing reads the list while a frame
     * is recorded. Main-thread only.
     */
    struct draw_list
    {
        /**
         * @brief Records the segment @p from – @p to in @p color for
         *        @p duration seconds, tested against the scene depth when
         *        @p depth_test is set and drawn on top of everything
         *        otherwise.
         */
        void add_line(const core::math::vec3& from,
                      const core::math::vec3& to,
                      const core::math::vec3& color,
                      float duration,
                      bool depth_test);

        /** @brief Records @p text at @p position in @p color for @p duration seconds. */
        void add_text(const core::math::vec3& position,
                      std::string_view text,
                      const core::math::vec3& color,
                      float duration);

        /**
         * @brief Ages every entry by @p seconds and drops those whose
         *        duration has run out, keeping the others in recording order.
         */
        void advance(float seconds);

        /** @brief Drops every entry. */
        void clear() noexcept;

        /**
         * @brief The live segments that are tested against the scene depth
         *        (@p depth_test) or drawn on top, in recording order.
         */
        std::span<const line_segment> lines(bool depth_test) const noexcept
        {
            return depth_test ? m_depth_tested_lines : m_on_top_lines;
        }

        /** @brief The live text labels, in recording order. */
        std::span<const text_label> texts() const noexcept
        {
            return m_texts;
        }

    private:
        std::vector<line_segment> m_on_top_lines;
        std::vector<line_segment> m_depth_tested_lines;
        std::vector<text_label> m_texts;
    };
} // namespace rendering_engine::debug_draw
