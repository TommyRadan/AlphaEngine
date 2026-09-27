// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file console_panel.cpp
 * @brief The Console panel: the recent log records, filtered by level
 *        and category.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

#include <imgui.h>

#include <core/log.hpp>
#include <core/os/os.hpp>

namespace editor
{
    namespace
    {
        ImVec4 console_level_color(core::logging::verbosity level)
        {
            switch (level)
            {
            case core::logging::verbosity::trace:
                return ImVec4{0.55f, 0.55f, 0.55f, 1.0f};
            case core::logging::verbosity::debug:
                return ImVec4{0.70f, 0.75f, 0.95f, 1.0f};
            case core::logging::verbosity::info:
                return ImVec4{0.85f, 0.85f, 0.85f, 1.0f};
            case core::logging::verbosity::warn:
                return ImVec4{0.95f, 0.75f, 0.20f, 1.0f};
            case core::logging::verbosity::error:
                return ImVec4{0.95f, 0.35f, 0.35f, 1.0f};
            case core::logging::verbosity::fatal:
                return ImVec4{1.00f, 0.20f, 0.20f, 1.0f};
            }
            return ImVec4{1.0f, 1.0f, 1.0f, 1.0f};
        }

        std::string format_timestamp(std::chrono::system_clock::time_point timestamp)
        {
            const std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
            std::tm local{};
            if (!core::os::local_time(time, local))
            {
                return "--:--:--";
            }
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
            return buffer;
        }
    } // namespace

    // The log ring buffer with a minimum-level and a
    // category-substring filter, auto-scroll and a clear button.
    void editor_layer::draw_console_window()
    {
        if (!m_show.console)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2{560.0f, 280.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Console", &m_show.console))
        {
            static constexpr std::array<const char*, 6> level_names = {
                "Trace", "Debug", "Info", "Warn", "Error", "Fatal"};
            ImGui::SetNextItemWidth(90.0f);
            ImGui::Combo(
                "##min_level", &m_console.level_filter, level_names.data(), static_cast<int>(level_names.size()));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##category_filter",
                                     "category filter",
                                     m_console.category_filter.data(),
                                     m_console.category_filter.size());
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &m_console.auto_scroll);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
            {
                core::logging::clear_recent_messages();
            }
            ImGui::Separator();

            if (ImGui::BeginChild("##console_scroll", ImVec2{0.0f, 0.0f}, false, ImGuiWindowFlags_HorizontalScrollbar))
            {
                for (const core::logging::record& record : core::logging::recent_messages())
                {
                    if (static_cast<int>(record.level) < m_console.level_filter)
                    {
                        continue;
                    }
                    if (m_console.category_filter[0] != '\0' &&
                        record.category.find(m_console.category_filter.data()) == std::string::npos)
                    {
                        continue;
                    }
                    const std::string line =
                        format_timestamp(record.timestamp) + " [" + record.category + "] " + record.text;
                    ImGui::PushStyleColor(ImGuiCol_Text, console_level_color(record.level));
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopStyleColor();
                }
                if (m_console.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
