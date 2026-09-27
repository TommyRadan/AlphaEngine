// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file profiler_panel.cpp
 * @brief The Profiler panel: the rolling frame-time graph and the GPU
 *        time of each pass.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <cstdio>

#include <imgui.h>

#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>

namespace editor
{
    // Frame-time profiler: plots the rolling history and reports the
    // min / max / average over the window so a hitch is visible at a
    // glance.
    void editor_layer::draw_profiler_window()
    {
        if (!m_show.profiler)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2{320.0f, 140.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Profiler", &m_show.profiler))
        {
            float min_ms = m_profiler.frame_times[0];
            float max_ms = m_profiler.frame_times[0];
            float sum_ms = 0.0f;
            for (const float sample : m_profiler.frame_times)
            {
                min_ms = sample < min_ms ? sample : min_ms;
                max_ms = sample > max_ms ? sample : max_ms;
                sum_ms += sample;
            }
            const float avg_ms = sum_ms / static_cast<float>(profiler_state::frame_history);

            char overlay[64];
            std::snprintf(overlay,
                          sizeof(overlay),
                          "avg %.2f ms (%.0f fps)",
                          static_cast<double>(avg_ms),
                          avg_ms > 0.0f ? 1000.0 / static_cast<double>(avg_ms) : 0.0);
            // Upper bound of the plot tracks the worst recent frame so
            // spikes stay on-scale; clamp to a sane floor.
            const float scale_max = max_ms > 1.0f ? max_ms * 1.2f : 1.2f;
            ImGui::PlotLines("##frame_times",
                             m_profiler.frame_times.data(),
                             profiler_state::frame_history,
                             m_profiler.frame_cursor,
                             overlay,
                             0.0f,
                             scale_max,
                             ImVec2{0.0f, 70.0f});
            ImGui::Text("min %.2f ms", static_cast<double>(min_ms));
            ImGui::SameLine();
            ImGui::Text("max %.2f ms", static_cast<double>(max_ms));

            // GPU time per pass from the device's
            // timestamp queries (last resolved frame).
            ImGui::SeparatorText("GPU");
            const rendering_engine::gpu_profiler& profiler = m_engine->renderer->get_gpu_profiler();
            if (!profiler.enabled())
            {
                ImGui::TextDisabled("no timestamp queries on this device");
            }
            else
            {
                ImGui::Text("frame %.2f ms", static_cast<double>(profiler.frame_gpu_ms()));
                if (ImGui::BeginTable("gpu_passes", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    for (const rendering_engine::gpu_pass_timing& timing : profiler.timings())
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(timing.name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.3f ms", static_cast<double>(timing.gpu_ms));
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::End();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
