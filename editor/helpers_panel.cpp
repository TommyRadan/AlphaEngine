// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file helpers_panel.cpp
 * @brief The Helpers panel: a toggle for the ground grid and for each of
 *        the editor's debug visualisations.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <imgui.h>

#include <rendering_engine/debug_draw/infinite_grid.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    // One checkbox per visualisation, plus show / hide all shortcuts. The
    // grid's checkbox is the renderer's grid proxy's visibility; the others
    // are what draw_debug_visuals records every frame, so a toggle takes
    // effect on the frame it is flipped in.
    void editor_layer::draw_helpers_window()
    {
        if (!m_show.helpers)
        {
            return;
        }

        rendering_engine::debug_draw::infinite_grid* grid = m_engine->renderer->editor_grid();

        ImGui::SetNextWindowSize(ImVec2{260.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Helpers", &m_show.helpers))
        {
            const auto set_all = [this, grid](bool shown)
            {
                if (grid != nullptr)
                {
                    grid->set_visible(shown);
                }
                m_visuals.axes = shown;
                m_visuals.physics = shown;
                m_visuals.lights = shown;
                m_visuals.cameras = shown;
            };
            if (ImGui::SmallButton("Show all"))
            {
                set_all(true);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hide all"))
            {
                set_all(false);
            }
            ImGui::Separator();

            if (grid != nullptr)
            {
                bool shown = grid->is_visible();
                if (ImGui::Checkbox("Grid", &shown))
                {
                    grid->set_visible(shown);
                }
            }
            ImGui::Checkbox("Axes", &m_visuals.axes);
            ImGui::Checkbox("Physics", &m_visuals.physics);
            ImGui::Checkbox("Lights", &m_visuals.lights);
            ImGui::Checkbox("Cameras", &m_visuals.cameras);
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
