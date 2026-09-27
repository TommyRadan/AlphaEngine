// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file scene_panel.cpp
 * @brief The Scene panel: what the scene pass submitted last frame and
 *        the live lights by type.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <cstdint>

#include <imgui.h>

#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/render_stats.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    // Scene statistics: how much geometry the scene pass submitted last
    // frame (models, draw calls, instances, triangles, vertices) plus a
    // breakdown of the live lights by type. The geometry figures come
    // from the renderer's per-frame @ref render_stats; the light counts
    // are read live from the lighting registry.
    void editor_layer::draw_scene_window()
    {
        if (!m_show.scene)
        {
            return;
        }

        const auto& stats = m_engine->renderer->get_render_stats();

        uint32_t ambient = 0;
        uint32_t directional = 0;
        uint32_t point = 0;
        uint32_t spot = 0;
        const auto& lights = rendering_engine::registered_lights();
        for (const auto* source : lights)
        {
            switch (source->type())
            {
            case rendering_engine::light_type::ambient:
                ++ambient;
                break;
            case rendering_engine::light_type::directional:
                ++directional;
                break;
            case rendering_engine::light_type::point:
                ++point;
                break;
            case rendering_engine::light_type::spot:
                ++spot;
                break;
            }
        }

        ImGui::SetNextWindowSize(ImVec2{300.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scene", &m_show.scene))
        {
            ImGui::SeparatorText("Geometry");
            ImGui::Text("Models in scene: %u", stats.scene_renderables);
            ImGui::Text("Models rendered: %u", stats.submitted);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Renderables whose bounds touch the camera frustum, plus any that report\n"
                                  "no bounds (fullscreen effects, gizmos) and are always drawn.");
            }
            ImGui::Text("Frustum culled: %u", stats.culled);
            ImGui::Text("Shadow culled: %u (omni %u, spot %u)",
                        stats.shadow_culled,
                        stats.point_shadow_culled,
                        stats.spot_shadow_culled);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Caster/cascade pairs skipped across the directional shadow cascades,\n"
                                  "caster/face pairs skipped across the six omni shadow faces, and\n"
                                  "casters skipped by the spot shadow pass.");
            }
            ImGui::Text("Draw calls: %u", stats.draw_calls);
            ImGui::Text("Instances: %u", stats.instances);
            ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(stats.triangles));
            ImGui::Text("Lines: %llu  Points: %llu",
                        static_cast<unsigned long long>(stats.lines),
                        static_cast<unsigned long long>(stats.points));
            ImGui::Text("Vertices: %llu", static_cast<unsigned long long>(stats.vertices));

            ImGui::SeparatorText("Lights");
            ImGui::Text("Total: %zu", lights.size());
            ImGui::Text("Ambient: %u", ambient);
            ImGui::Text("Directional: %u", directional);
            ImGui::Text("Point: %u", point);
            ImGui::Text("Spot: %u", spot);
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
