// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file post_panel.cpp
 * @brief The Post panel: live tuning of the post-processing chain.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <array>
#include <cstdio>

#include <imgui.h>

#include <rendering_engine/post_settings.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    // Runtime tuning for the post-processing chain: tonemap
    // exposure/operator, auto exposure, colour grading, volumetric fog,
    // motion blur, bloom, temporal AA feedback and FXAA. Every edit is
    // written back through renderer::set_post_settings, the same path
    // any other caller would use, so it takes effect on the next
    // recorded frame (tonemap immediately, since its setters rewrite
    // their UBO on the spot). TAA's own enabled checkbox is shown
    // disabled: the pass is only ever brought up once, at init, from
    // graphics.temporal_aa (see post_settings::taa's doc comment).
    // The panel edits the live settings only: core::load_settings has no
    // save path, so the startup values stay whatever settings.json,
    // the environment and the command line resolved.
    void editor_layer::draw_post_window()
    {
        if (!m_show.post)
        {
            return;
        }

        auto& renderer = *m_engine->renderer;
        rendering_engine::post_settings settings = renderer.get_post_settings();
        bool changed = false;

        ImGui::SetNextWindowSize(ImVec2{300.0f, 0.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Post", &m_show.post))
        {
            ImGui::SeparatorText("Tonemap");
            ImGui::BeginDisabled(settings.auto_exposure.enabled);
            changed |= ImGui::SliderFloat("Exposure", &settings.exposure, 0.0f, 8.0f);
            ImGui::EndDisabled();
            if (settings.auto_exposure.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Driven by auto exposure; tune its compensation instead");
            }
            static constexpr std::array<const char*, 3> operator_names = {"None", "Reinhard", "ACES"};
            int op = static_cast<int>(settings.tonemap_op);
            if (ImGui::Combo("Operator", &op, operator_names.data(), static_cast<int>(operator_names.size())))
            {
                settings.tonemap_op = static_cast<rendering_engine::tonemap_operator>(op);
                changed = true;
            }

            ImGui::SeparatorText("Auto exposure");
            changed |= ImGui::Checkbox("Enabled##auto_exposure", &settings.auto_exposure.enabled);
            changed |= ImGui::SliderFloat("Min EV100", &settings.auto_exposure.min_ev, -16.0f, 32.0f);
            changed |= ImGui::SliderFloat("Max EV100", &settings.auto_exposure.max_ev, -16.0f, 32.0f);
            changed |= ImGui::SliderFloat("Speed up", &settings.auto_exposure.speed_up, 0.0f, 10.0f);
            changed |= ImGui::SliderFloat("Speed down", &settings.auto_exposure.speed_down, 0.0f, 10.0f);
            changed |= ImGui::SliderFloat("Compensation", &settings.auto_exposure.compensation, -8.0f, 8.0f);

            ImGui::SeparatorText("Colour grading");
            // Mirror the live path unless the field is being edited, so
            // a path set elsewhere shows up and a half-typed one is not
            // clobbered; the edit applies once committed with Enter.
            if (!m_post.grading_lut_editing)
            {
                std::snprintf(m_post.grading_lut_input.data(),
                              m_post.grading_lut_input.size(),
                              "%s",
                              settings.grading.lut.c_str());
            }
            if (ImGui::InputText("LUT",
                                 m_post.grading_lut_input.data(),
                                 m_post.grading_lut_input.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                settings.grading.lut = m_post.grading_lut_input.data();
                changed = true;
            }
            m_post.grading_lut_editing = ImGui::IsItemActive();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("N*N x N strip LUT image; empty turns grading off (Enter applies)");
            }
            changed |= ImGui::SliderFloat("Intensity##grading", &settings.grading.intensity, 0.0f, 1.0f);
            if (settings.grading.lut.empty())
            {
                ImGui::TextDisabled("No LUT: grading off");
            }
            else if (!renderer.grading_lut_loaded())
            {
                ImGui::TextDisabled("LUT not loaded (see the log)");
            }

            ImGui::SeparatorText("Volumetric fog");
            changed |= ImGui::Checkbox("Enabled##volumetric", &settings.volumetric.enabled);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Raymarches the scene's height fog: needs fog_settings::height_density > 0");
            }
            changed |= ImGui::SliderFloat("Density scale", &settings.volumetric.density_scale, 0.0f, 4.0f);
            changed |= ImGui::SliderFloat("Anisotropy", &settings.volumetric.anisotropy, -0.95f, 0.95f);
            changed |= ImGui::SliderFloat("Max distance", &settings.volumetric.max_distance, 1.0f, 256.0f);
            changed |= ImGui::SliderInt("Steps", &settings.volumetric.steps, 4, 128);
            changed |= ImGui::SliderFloat("Intensity", &settings.volumetric.intensity, 0.0f, 8.0f);

            ImGui::SeparatorText("Motion blur");
            changed |= ImGui::Checkbox("Enabled##motion_blur", &settings.motion_blur.enabled);
            changed |= ImGui::SliderFloat("Shutter##motion_blur", &settings.motion_blur.intensity, 0.0f, 2.0f);
            changed |= ImGui::SliderInt("Samples##motion_blur", &settings.motion_blur.samples, 2, 32);
            changed |=
                ImGui::SliderFloat("Max radius (px)##motion_blur", &settings.motion_blur.max_radius, 1.0f, 128.0f);

            ImGui::SeparatorText("Bloom");
            changed |= ImGui::Checkbox("Enabled##bloom", &settings.bloom.enabled);
            changed |= ImGui::SliderFloat("Threshold", &settings.bloom.threshold, 0.0f, 4.0f);
            changed |= ImGui::SliderFloat("Knee", &settings.bloom.knee, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Strength", &settings.bloom.strength, 0.0f, 2.0f);

            ImGui::SeparatorText("Temporal AA");
            bool taa_enabled = settings.taa.enabled;
            ImGui::BeginDisabled(true);
            ImGui::Checkbox("Enabled##taa", &taa_enabled);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Fixed at startup by graphics.temporal_aa");
            }
            changed |= ImGui::SliderFloat("Feedback", &settings.taa.feedback, 0.0f, 0.99f);

            ImGui::SeparatorText("FXAA");
            changed |= ImGui::Checkbox("Enabled##fxaa", &settings.fxaa.enabled);
        }
        ImGui::End();

        if (changed)
        {
            renderer.set_post_settings(settings);
        }
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
