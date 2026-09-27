// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file helpers_panel.cpp
 * @brief The Helpers panel: a visibility toggle for every live debug-
 *        draw helper.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <imgui.h>

#include <rendering_engine/debug_draw/helper.hpp>

namespace editor
{
    // Lists every live debug gizmo with
    // a checkbox bound to its visibility, plus master show / hide
    // shortcuts. The helper registry is shared with the renderer, so
    // the toggles take effect on the next debug-pass draw.
    void editor_layer::draw_helpers_window()
    {
        if (!m_show.helpers)
        {
            return;
        }

        const auto& helpers = rendering_engine::debug_draw::registered_helpers();

        ImGui::SetNextWindowSize(ImVec2{260.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Helpers", &m_show.helpers))
        {
            if (helpers.empty())
            {
                ImGui::TextDisabled("no debug helpers registered");
            }
            else
            {
                if (ImGui::SmallButton("Show all"))
                {
                    for (auto* gizmo : helpers)
                    {
                        gizmo->visible = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Hide all"))
                {
                    for (auto* gizmo : helpers)
                    {
                        gizmo->visible = false;
                    }
                }
                ImGui::Separator();

                // Disambiguate the checkbox ids by index so two
                // helpers sharing a name still toggle independently.
                int index = 0;
                for (auto* gizmo : helpers)
                {
                    ImGui::PushID(index++);
                    ImGui::Checkbox(gizmo->name(), &gizmo->visible);
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
