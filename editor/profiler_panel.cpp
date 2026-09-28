// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file profiler_panel.cpp
 * @brief The Profiler panel: the game's time controls, the rolling
 *        frame-time graph, the CPU time of each scheduler stage and system,
 *        and the GPU time of each pass.
 */

#ifdef ALPHAENGINE_HAS_IMGUI

#include <editor/editor_layer.hpp>

#include <cstddef>
#include <cstdio>
#include <vector>

#include <imgui.h>

#include <core/time.hpp>
#include <rendering_engine/gpu_profiler.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>
#include <runtime/scheduler.hpp>

namespace editor
{
    namespace
    {
        // Pause, resume and the time scale of the game. The editor itself
        // runs on real time, so it stays responsive while the game is paused.
        void draw_time_controls(core::time& clock, const runtime::scheduler& systems)
        {
            ImGui::SeparatorText("Time");
            bool paused = clock.is_paused();
            if (ImGui::Checkbox("Paused", &paused))
            {
                clock.set_paused(paused);
            }
            ImGui::SameLine();
            if (ImGui::Button("Real time"))
            {
                clock.set_time_scale(1.0);
            }
            auto scale = static_cast<float>(clock.time_scale());
            if (ImGui::SliderFloat("Time scale", &scale, 0.0f, 4.0f, "%.2f"))
            {
                clock.set_time_scale(static_cast<double>(scale));
            }
            ImGui::Text("fixed steps this frame: %u", systems.fixed_steps());
        }

        // CPU time of every stage of the latest complete frame, each
        // unfolding into its systems.
        void draw_cpu_timings(const runtime::scheduler& systems)
        {
            ImGui::SeparatorText("CPU");
            const auto& stages = systems.stage_timings();
            const std::vector<runtime::system_timing>& timings = systems.system_timings();
            if (!ImGui::BeginTable("cpu_stages", 3, ImGuiTableFlags_SizingStretchProp))
            {
                return;
            }
            for (std::size_t index = 0; index < runtime::stage_count; ++index)
            {
                const auto where = static_cast<runtime::stage>(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool open = ImGui::TreeNodeEx(runtime::stage_name(where), ImGuiTreeNodeFlags_SpanFullWidth);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f ms", stages[index].milliseconds);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("x%u", stages[index].runs);
                if (!open)
                {
                    continue;
                }
                for (const runtime::system_timing& timing : timings)
                {
                    if (timing.where != where)
                    {
                        continue;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Indent();
                    ImGui::TextUnformatted(timing.name.c_str());
                    ImGui::Unindent();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.3f ms", timing.milliseconds);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("x%u", timing.runs);
                }
                ImGui::TreePop();
            }
            ImGui::EndTable();
        }
    } // namespace

    // Frame-time profiler: the game's time controls, then the rolling
    // history with the min / max / average over the window so a hitch is
    // visible at a glance, then where the CPU and the GPU spent the frame.
    void editor_layer::draw_profiler_window()
    {
        if (!m_show.profiler)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2{340.0f, 420.0f}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Profiler", &m_show.profiler))
        {
            draw_time_controls(*m_engine->time, *m_engine->systems);

            ImGui::SeparatorText("Frame");
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

            draw_cpu_timings(*m_engine->systems);

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
