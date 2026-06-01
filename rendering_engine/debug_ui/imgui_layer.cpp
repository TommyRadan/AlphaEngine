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

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/window.hpp>
#include <SDL3/SDL.h>

namespace rendering_engine::debug_ui
{
    namespace
    {
        // True once ImGui and its backends are live. Stays false on the
        // Vulkan backend (unsupported for now) so every entry point below
        // becomes an early-out.
        bool g_active = false;

        // Visibility toggles for the optional panels, driven from the
        // overlay's right-click context menu.
        bool g_show_settings = true;
        bool g_show_demo = false;

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
                    ImGui::MenuItem("Settings", nullptr, &g_show_settings);
                    ImGui::MenuItem("ImGui demo", nullptr, &g_show_demo);
                    ImGui::EndPopup();
                }
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
    } // namespace

    void init()
    {
        if (g_active)
        {
            return;
        }

        auto& eng = control::current_engine();
        if (eng.settings->get_graphics_backend() != graphics_backend::opengl)
        {
            LOG_WRN("debug_ui: ImGui overlay is only wired for the OpenGL backend; skipping on this backend");
            return;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplSDL3_InitForOpenGL(eng.window->sdl_window(), eng.window->gl_context()))
        {
            LOG_ERR("debug_ui: ImGui_ImplSDL3_InitForOpenGL failed");
            ImGui::DestroyContext();
            return;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 460"))
        {
            LOG_ERR("debug_ui: ImGui_ImplOpenGL3_Init failed");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        g_active = true;
        LOG_INF("debug_ui: ImGui overlay initialised (SDL3 + OpenGL3)");
    }

    void shutdown()
    {
        if (!g_active)
        {
            return;
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        g_active = false;
        LOG_INF("debug_ui: ImGui overlay shut down");
    }

    void process_event(const void* sdl_event)
    {
        if (!g_active || sdl_event == nullptr)
        {
            return;
        }
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
    }

    void render()
    {
        if (!g_active)
        {
            return;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        draw_fps_overlay();
        draw_settings_window();
        if (g_show_demo)
        {
            ImGui::ShowDemoWindow(&g_show_demo);
        }

        ImGui::Render();
        // The rendering passes leave the default framebuffer bound, but
        // bind it explicitly so the overlay never lands on a stale FBO.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    bool wants_keyboard()
    {
        return g_active && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool wants_mouse()
    {
        return g_active && ImGui::GetIO().WantCaptureMouse;
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
    void render() {}

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
