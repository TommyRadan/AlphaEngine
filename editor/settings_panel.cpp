// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings_panel.cpp
 * @brief The Settings panel: the resolved engine settings, read-only.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <imgui.h>

#include <platform/window_settings.hpp>
#include <rendering_engine/graphics_settings.hpp>
#include <runtime/engine.hpp>
#include <runtime/engine_settings.hpp>

namespace editor
{
    // Read-only inspector for the engine-wide settings object. The
    // accessors are const, so the panel reports the resolved
    // configuration rather than editing it.
    void editor_layer::draw_settings_window()
    {
        if (!m_show.settings)
        {
            return;
        }

        const auto& settings = *m_engine->settings;
        ImGui::SetNextWindowSize(ImVec2{320.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", &m_show.settings))
        {
            ImGui::SeparatorText("Window");
            ImGui::Text("Size: %u x %u", settings.window.width, settings.window.height);
            ImGui::Text("Aspect: %.3f", static_cast<double>(settings.window.aspect_ratio()));
            ImGui::Text("Mode: %s", platform::window_mode_name(settings.window.mode));
            ImGui::Text("Vsync: %s", settings.window.vsync ? "on" : "off");

            ImGui::SeparatorText("Camera / input");
            ImGui::Text("Field of view: %.1f", static_cast<double>(settings.camera.field_of_view));
            ImGui::Text("Mouse sensitivity: %.4f", static_cast<double>(settings.input.mouse_sensitivity));
            ImGui::Text("Mouse reversed: %s", settings.input.mouse_reversed ? "yes" : "no");
            ImGui::Text("Max players: %u", settings.input.max_players);

            ImGui::SeparatorText("GPU");
            ImGui::Text("Backend: %s", rendering_engine::graphics_backend_name(settings.graphics.backend));
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
