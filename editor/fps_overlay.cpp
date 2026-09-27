// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file fps_overlay.cpp
 * @brief The always-on FPS overlay, whose context menu opens and closes
 *        the other panels.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <imgui.h>

#include <core/time.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    // Small always-on overlay pinned to the top-right corner showing
    // the frame rate and frame time. Right-clicking it toggles the
    // heavier inspector panels.
    void editor_layer::draw_fps_overlay()
    {
        const auto& time = *m_engine->time;

        constexpr float pad = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 work_pos = viewport->WorkPos;
        const ImVec2 work_size = viewport->WorkSize;
        const ImVec2 position{work_pos.x + work_size.x - pad, work_pos.y + pad};
        ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{1.0f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.35f);

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("fps_overlay", nullptr, flags))
        {
            ImGui::Text("FPS: %.0f", static_cast<double>(time.current_fps()));
            ImGui::Text("Frame: %.2f ms", time.delta_time());
            ImGui::Separator();
            ImGui::TextDisabled("right-click for tools");
            if (ImGui::BeginPopupContextWindow())
            {
                ImGui::MenuItem("Render Targets", nullptr, &m_show.render_targets);
                ImGui::MenuItem("Console", nullptr, &m_show.console);
                ImGui::MenuItem("Hierarchy", nullptr, &m_show.hierarchy);
                ImGui::MenuItem("Inspector", nullptr, &m_show.inspector);
                ImGui::MenuItem("Profiler", nullptr, &m_show.profiler);
                ImGui::MenuItem("Scene", nullptr, &m_show.scene);
                ImGui::MenuItem("Settings", nullptr, &m_show.settings);
                ImGui::MenuItem("Post", nullptr, &m_show.post);
                ImGui::MenuItem("Helpers", nullptr, &m_show.helpers);
                ImGui::MenuItem("ImGui demo", nullptr, &m_show.demo);
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
