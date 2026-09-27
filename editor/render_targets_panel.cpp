// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file render_targets_panel.cpp
 * @brief The Render Targets panel: the engine's intermediate textures,
 *        shown through the overlay renderer.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <imgui.h>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/overlay_renderer.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    namespace
    {
        // One texture the viewer can show, paired with the label it lists
        // it under. Built fresh every frame from the renderer's read-only
        // accessors, so a target that comes and goes (temporal AA off, no
        // environment set) appears and disappears with it.
        struct render_target_slot
        {
            const char* label;
            rendering_engine::gpu::texture texture;
        };
    } // namespace

    // ImGui::Image over the engine's intermediate textures: the HDR
    // scene colour, scene depth, the tonemapped LDR image, the spot
    // shadow map, the velocity buffer, the temporal-AA resolve and
    // the active environment's BRDF LUT. Every off-screen render
    // target the engine currently exposes as a plain 2D, colour-or-
    // depth texture ends up here; the ones that do not fit that shape
    // (the directional light's cascade array, the point-light cube
    // shadow map, the prefiltered / irradiance IBL cubes) are listed
    // but not shown — ImGui's image shader has no array- or
    // cube-sampling path.
    void editor_layer::draw_render_targets_window()
    {
        if (!m_show.render_targets)
        {
            return;
        }

        auto& renderer = *m_engine->renderer;
        const std::array<render_target_slot, 7> slots = {{
            {"HDR scene colour", renderer.scene_color_texture()},
            {"Scene depth", renderer.scene_depth_texture()},
            {"LDR colour (tonemapped)", renderer.ldr_color_texture()},
            {"Spot shadow map", renderer.spot_shadow_map()},
            {"Velocity", renderer.velocity_texture()},
            {"TAA resolve", renderer.taa_resolve_texture()},
            {"IBL BRDF LUT", renderer.environment_brdf_lut()},
        }};

        // Resolved (and registered / pruned) every frame regardless of
        // whether the window is open or collapsed, so the overlay
        // renderer's registrations always track exactly the handles
        // currently in play rather than whatever was last drawn.
        std::vector<uint64_t> touched;
        std::array<rendering_engine::gpu::overlay_texture, slots.size()> resolved{};
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i].texture.valid())
            {
                resolved[i] = m_overlay->texture(slots[i].texture);
                touched.push_back(slots[i].texture.id);
            }
        }
        m_overlay->retain_textures(touched);

        ImGui::SetNextWindowSize(ImVec2{420.0f, 380.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Render Targets", &m_show.render_targets))
        {
            const render_target_slot& current = slots[static_cast<std::size_t>(m_render_target_selected)];
            if (ImGui::BeginCombo("##render_target_select", current.label))
            {
                for (int i = 0; i < static_cast<int>(slots.size()); ++i)
                {
                    const bool is_selected = (m_render_target_selected == i);
                    if (ImGui::Selectable(slots[static_cast<std::size_t>(i)].label, is_selected))
                    {
                        m_render_target_selected = i;
                    }
                    if (is_selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            const rendering_engine::gpu::overlay_texture& shown =
                resolved[static_cast<std::size_t>(m_render_target_selected)];
            if (shown.id == 0)
            {
                ImGui::TextDisabled("not active");
            }
            else
            {
                const float aspect =
                    shown.height > 0 ? static_cast<float>(shown.width) / static_cast<float>(shown.height) : 1.0f;
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::Image(static_cast<ImTextureID>(shown.id), ImVec2{width, aspect > 0.0f ? width / aspect : width});
                ImGui::Text("%u x %u", shown.width, shown.height);
            }

            ImGui::Separator();
            ImGui::TextDisabled("Not shown: directional shadow (cascade array), point-light shadow (cube),\n"
                                "prefiltered / irradiance IBL (cube)");
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
