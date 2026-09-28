// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/draw_list.hpp>

#include <algorithm>

namespace rendering_engine::debug_draw
{
    namespace
    {
        // Ages every entry by @p seconds and drops the expired ones: an entry
        // expires once no time is left, so one recorded with no duration
        // lives until the next tick ages it.
        template<typename Entry>
        void age(std::vector<Entry>& entries, float seconds)
        {
            for (Entry& entry : entries)
            {
                entry.remaining -= seconds;
            }
            std::erase_if(entries, [](const Entry& entry) { return entry.remaining <= 0.0f; });
        }
    } // namespace

    void draw_list::add_line(const core::math::vec3& from,
                             const core::math::vec3& to,
                             const core::math::vec3& color,
                             float duration,
                             bool depth_test)
    {
        (depth_test ? m_depth_tested_lines : m_on_top_lines)
            .push_back(line_segment{from, to, color, std::max(duration, 0.0f)});
    }

    void draw_list::add_text(const core::math::vec3& position,
                             std::string_view text,
                             const core::math::vec3& color,
                             float duration)
    {
        m_texts.push_back(text_label{position, std::string{text}, color, std::max(duration, 0.0f)});
    }

    void draw_list::advance(float seconds)
    {
        age(m_on_top_lines, seconds);
        age(m_depth_tested_lines, seconds);
        age(m_texts, seconds);
    }

    void draw_list::clear() noexcept
    {
        m_on_top_lines.clear();
        m_depth_tested_lines.clear();
        m_texts.clear();
    }
} // namespace rendering_engine::debug_draw
