/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rendering_engine/debug_ui/imgui_layer.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI

#include <array>
#include <cstddef>
#include <cstdio>

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_resources.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/window.hpp>
#include <SDL3/SDL.h>

namespace rendering_engine::debug_ui
{
    namespace
    {
        // Which renderer backend the overlay is driving. Stays @c none
        // until @ref init succeeds, so every entry point early-outs when
        // ImGui is not live.
        enum class backend_mode
        {
            none,
            opengl,
            vulkan,
        };

        backend_mode g_backend = backend_mode::none;

        // Set by begin_frame() once ImGui::Render() has produced draw
        // data, cleared after the draw data is recorded in the debug
        // pass. Guards against recording a half-built frame.
        bool g_frame_ready = false;

        // Visibility toggles for the optional panels, driven from the
        // FPS overlay's right-click context menu.
        bool g_show_settings = true;
        bool g_show_profiler = true;
        bool g_show_demo = false;

        // Rolling frame-time history (milliseconds) for the profiler
        // graph, used as a ring buffer.
        constexpr int k_frame_history = 120;
        std::array<float, k_frame_history> g_frame_times{};
        int g_frame_cursor = 0;

        void check_vk_result(VkResult result)
        {
            if (result != VK_SUCCESS)
            {
                LOG_ERR("debug_ui: Vulkan error in ImGui backend (VkResult=%d)", static_cast<int>(result));
            }
        }

        const char* window_type_name(win_type type)
        {
            switch (type)
            {
            case win_type::win_type_windowed:
                return "windowed";
            case win_type::win_type_borderless:
                return "borderless";
            case win_type::win_type_fullscreen:
                return "fullscreen";
            }
            return "unknown";
        }

        const char* graphics_backend_name(graphics_backend backend)
        {
            switch (backend)
            {
            case graphics_backend::opengl:
                return "opengl";
            case graphics_backend::vulkan:
                return "vulkan";
            }
            return "unknown";
        }

        // Small always-on overlay pinned to the top-right corner showing
        // the frame rate and frame time. Right-clicking it toggles the
        // heavier inspector panels.
        void draw_fps_overlay()
        {
            const auto& time = *control::current_engine().time;

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
                    ImGui::MenuItem("Profiler", nullptr, &g_show_profiler);
                    ImGui::MenuItem("Settings", nullptr, &g_show_settings);
                    ImGui::MenuItem("ImGui demo", nullptr, &g_show_demo);
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }

        // Frame-time profiler: plots the rolling history and reports the
        // min / max / average over the window so a hitch is visible at a
        // glance.
        void draw_profiler_window()
        {
            if (!g_show_profiler)
            {
                return;
            }

            ImGui::SetNextWindowSize(ImVec2{320.0f, 140.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Profiler", &g_show_profiler))
            {
                float min_ms = g_frame_times[0];
                float max_ms = g_frame_times[0];
                float sum_ms = 0.0f;
                for (const float sample : g_frame_times)
                {
                    min_ms = sample < min_ms ? sample : min_ms;
                    max_ms = sample > max_ms ? sample : max_ms;
                    sum_ms += sample;
                }
                const float avg_ms = sum_ms / static_cast<float>(k_frame_history);

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
                                 g_frame_times.data(),
                                 k_frame_history,
                                 g_frame_cursor,
                                 overlay,
                                 0.0f,
                                 scale_max,
                                 ImVec2{0.0f, 70.0f});
                ImGui::Text("min %.2f ms", static_cast<double>(min_ms));
                ImGui::SameLine();
                ImGui::Text("max %.2f ms", static_cast<double>(max_ms));
            }
            ImGui::End();
        }

        // Read-only inspector for the engine-wide settings object. The
        // accessors are const, so the panel reports the resolved
        // configuration rather than editing it.
        void draw_settings_window()
        {
            if (!g_show_settings)
            {
                return;
            }

            const auto& settings = *control::current_engine().settings;
            ImGui::SetNextWindowSize(ImVec2{320.0f, 0.0f}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Settings", &g_show_settings))
            {
                ImGui::SeparatorText("Window");
                ImGui::Text("Size: %u x %u", settings.get_window_width(), settings.get_window_height());
                ImGui::Text("Aspect: %.3f", static_cast<double>(settings.get_aspect_ratio()));
                ImGui::Text("Mode: %s", window_type_name(settings.get_window_type()));
                ImGui::Text("Double buffered: %s", settings.is_double_buffered() ? "yes" : "no");
                ImGui::Text("Vsync: %s", settings.is_vsync_enabled() ? "on" : "off");

                ImGui::SeparatorText("Camera / input");
                ImGui::Text("Field of view: %.1f", static_cast<double>(settings.get_field_of_view()));
                ImGui::Text("Mouse sensitivity: %.4f", static_cast<double>(settings.get_mouse_sensitivity()));
                ImGui::Text("Mouse reversed: %s", settings.is_mouse_reversed() ? "yes" : "no");

                ImGui::SeparatorText("GPU");
                ImGui::Text("Backend: %s", graphics_backend_name(settings.get_graphics_backend()));
            }
            ImGui::End();
        }

        void build_panels()
        {
            // Push this frame's time into the rolling history first so the
            // profiler reflects the live cadence.
            const auto& time = *control::current_engine().time;
            g_frame_times[g_frame_cursor] = static_cast<float>(time.delta_time());
            g_frame_cursor = (g_frame_cursor + 1) % k_frame_history;

            draw_fps_overlay();
            draw_profiler_window();
            draw_settings_window();
            if (g_show_demo)
            {
                ImGui::ShowDemoWindow(&g_show_demo);
            }
        }

        // Record the built draw data into the swapchain-targeted debug
        // pass. Fired from the @ref event_engine::render_debug listener
        // while the render pass is still open.
        void on_render_debug(const event_engine::render_debug& event)
        {
            if (g_backend == backend_mode::none || !g_frame_ready)
            {
                return;
            }
            ImDrawData* draw_data = ImGui::GetDrawData();
            if (draw_data == nullptr)
            {
                return;
            }

            if (g_backend == backend_mode::opengl)
            {
                // The debug pass left the swapchain framebuffer bound;
                // the immediate-mode GL backend draws straight into it.
                ImGui_ImplOpenGL3_RenderDrawData(draw_data);
            }
            else if (g_backend == backend_mode::vulkan && event.encoder != nullptr)
            {
                auto* cmd = static_cast<VkCommandBuffer>(event.encoder->native_command_buffer());
                if (cmd != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
                }
            }
            g_frame_ready = false;
        }

        bool init_vulkan(control::engine& eng)
        {
            auto* device = static_cast<gpu::backend::vulkan::vk_device*>(eng.gpu.get());

            // Acquire the same render pass the debug pass draws into:
            // swapchain target, colour loaded (the UI/scene already
            // composited), no depth. ImGui builds its pipeline against
            // this pass, so it must match what the debug pass begins.
            const gpu::render_target swapchain = device->swapchain_target();
            auto* target = device->lookup_render_target(swapchain);
            if (target == nullptr)
            {
                LOG_ERR("debug_ui: no swapchain render target for ImGui Vulkan init");
                return false;
            }
            // Match the debug pass's descriptor exactly so the cache hands
            // back the same VkRenderPass it begins with: colour loaded, no
            // depth. The debug pass leaves depth.load at its default
            // (clear), and acquire_render_pass keys on it even when depth
            // is unused, so pass the same value here.
            VkRenderPass ui_render_pass = device->acquire_render_pass(
                *target, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR, /*use_depth=*/false);
            if (ui_render_pass == VK_NULL_HANDLE)
            {
                LOG_ERR("debug_ui: acquire_render_pass returned null for ImGui Vulkan init");
                return false;
            }

            if (!ImGui_ImplSDL3_InitForVulkan(eng.window->sdl_window()))
            {
                LOG_ERR("debug_ui: ImGui_ImplSDL3_InitForVulkan failed");
                return false;
            }

            const uint32_t image_count = device->swapchain_image_count();
            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion = VK_API_VERSION_1_0;
            init_info.Instance = device->instance();
            init_info.PhysicalDevice = device->physical_device();
            init_info.Device = device->vk_handle();
            init_info.QueueFamily = device->graphics_queue_family();
            init_info.Queue = device->graphics_queue();
            // Leave DescriptorPool null and let the backend own a pool
            // sized for the font atlas (and any user textures); avoids
            // depending on the engine pool's descriptor budget / flags.
            init_info.DescriptorPool = VK_NULL_HANDLE;
            init_info.DescriptorPoolSize = 16;
            init_info.MinImageCount = image_count < 2 ? 2 : image_count;
            init_info.ImageCount = image_count < 2 ? 2 : image_count;
            init_info.PipelineCache = VK_NULL_HANDLE;
            init_info.PipelineInfoMain.RenderPass = ui_render_pass;
            init_info.PipelineInfoMain.Subpass = 0;
            init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering = false;
            init_info.Allocator = nullptr;
            init_info.CheckVkResultFn = check_vk_result;
            if (!ImGui_ImplVulkan_Init(&init_info))
            {
                LOG_ERR("debug_ui: ImGui_ImplVulkan_Init failed");
                ImGui_ImplSDL3_Shutdown();
                return false;
            }

            g_backend = backend_mode::vulkan;
            return true;
        }

        bool init_opengl(control::engine& eng)
        {
            if (!ImGui_ImplSDL3_InitForOpenGL(eng.window->sdl_window(), eng.window->gl_context()))
            {
                LOG_ERR("debug_ui: ImGui_ImplSDL3_InitForOpenGL failed");
                return false;
            }
            if (!ImGui_ImplOpenGL3_Init("#version 460"))
            {
                LOG_ERR("debug_ui: ImGui_ImplOpenGL3_Init failed");
                ImGui_ImplSDL3_Shutdown();
                return false;
            }
            g_backend = backend_mode::opengl;
            return true;
        }
    } // namespace

    void init()
    {
        if (g_backend != backend_mode::none)
        {
            return;
        }

        auto& eng = control::current_engine();
        const graphics_backend backend = eng.settings->get_graphics_backend();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        bool ok = false;
        if (backend == graphics_backend::vulkan)
        {
            ok = init_vulkan(eng);
        }
        else
        {
            ok = init_opengl(eng);
        }

        if (!ok)
        {
            ImGui::DestroyContext();
            g_backend = backend_mode::none;
            return;
        }

        // Record the overlay into the swapchain-targeted debug pass. The
        // debug pass emits render_debug while its render pass is open, so
        // both backends land their draws on top of the composited frame.
        eng.events->subscribe<event_engine::render_debug>(on_render_debug);

        LOG_INF("debug_ui: ImGui overlay initialised (SDL3 + %s)", graphics_backend_name(backend));
    }

    void shutdown()
    {
        if (g_backend == backend_mode::none)
        {
            return;
        }
        if (g_backend == backend_mode::vulkan)
        {
            // The render queue must be idle before tearing the backend's
            // GPU resources down.
            auto* device = static_cast<gpu::backend::vulkan::vk_device*>(control::current_engine().gpu.get());
            vkDeviceWaitIdle(device->vk_handle());
            ImGui_ImplVulkan_Shutdown();
        }
        else
        {
            ImGui_ImplOpenGL3_Shutdown();
        }
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        g_backend = backend_mode::none;
        g_frame_ready = false;
        LOG_INF("debug_ui: ImGui overlay shut down");
    }

    void process_event(const void* sdl_event)
    {
        if (g_backend == backend_mode::none || sdl_event == nullptr)
        {
            return;
        }
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
    }

    void begin_frame()
    {
        if (g_backend == backend_mode::none)
        {
            return;
        }

        if (g_backend == backend_mode::vulkan)
        {
            ImGui_ImplVulkan_NewFrame();
        }
        else
        {
            ImGui_ImplOpenGL3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        build_panels();

        ImGui::Render();
        g_frame_ready = true;
    }

    bool wants_keyboard()
    {
        return g_backend != backend_mode::none && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool wants_mouse()
    {
        return g_backend != backend_mode::none && ImGui::GetIO().WantCaptureMouse;
    }
} // namespace rendering_engine::debug_ui

#else // ALPHAENGINE_HAS_IMGUI

// Release builds (and any configuration without ImGui) get inert stubs so
// the always-compiled engine core can call the layer unconditionally.
namespace rendering_engine::debug_ui
{
    void init() {}
    void shutdown() {}
    void process_event(const void*) {}
    void begin_frame() {}

    bool wants_keyboard()
    {
        return false;
    }

    bool wants_mouse()
    {
        return false;
    }
} // namespace rendering_engine::debug_ui

#endif // ALPHAENGINE_HAS_IMGUI
