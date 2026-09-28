// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file editor.cpp
 * @brief The editor's core: the ImGui context and its backends, the
 *        window's event filter, the dockspace every panel docks into, and
 *        the per-frame build. The panels themselves are in their own files.
 */

#include <editor/editor.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI

#include <memory>
#include <string>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/time.hpp>
#include <editor/editor_layer.hpp>
#include <platform/platform.hpp>
#include <platform/window.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/overlay_renderer.hpp>
#include <rendering_engine/renderer.hpp>
#include <runtime/engine.hpp>
#include <SDL3/SDL.h>

namespace editor
{
    namespace
    {
        // Arranges the built-in panels into a hierarchy / inspector /
        // render-targets / console layout around a central, transparent
        // dock node (so the game view shows through it). Only called once,
        // the first time this dockspace id has no node — a fresh
        // imgui.ini, or one predating this dockspace — so a user's own
        // rearrangement, once saved, is never overwritten.
        void build_default_dock_layout(ImGuiID dockspace_id)
        {
            ImGui::DockBuilderRemoveNode(dockspace_id);
            // ImGuiDockNodeFlags_DockSpace is the internal-only flag
            // DockBuilderAddNode expects for a dockspace root; the cast
            // avoids an enum-mismatch warning ORing it with the public
            // ImGuiDockNodeFlags_PassthruCentralNode.
            ImGui::DockBuilderAddNode(dockspace_id,
                                      static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) |
                                          ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            ImGuiID main_id = dockspace_id;
            ImGuiID right_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Right, 0.26f, nullptr, &main_id);
            ImGuiID bottom_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.28f, nullptr, &main_id);
            ImGuiID left_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Left, 0.2f, nullptr, &main_id);
            ImGuiID right_bottom_id = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.5f, nullptr, &right_id);

            ImGui::DockBuilderDockWindow("Hierarchy", left_id);
            ImGui::DockBuilderDockWindow("Inspector", right_id);
            ImGui::DockBuilderDockWindow("Render Targets", right_bottom_id);
            ImGui::DockBuilderDockWindow("Console", bottom_id);
            ImGui::DockBuilderDockWindow("Profiler", main_id);
            ImGui::DockBuilderDockWindow("Scene", main_id);
            ImGui::DockBuilderDockWindow("Settings", main_id);
            ImGui::DockBuilderDockWindow("Post", main_id);
            ImGui::DockBuilderDockWindow("Helpers", main_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    } // namespace

    editor_layer::~editor_layer()
    {
        shutdown();
    }

    void editor_layer::init(runtime::engine& eng)
    {
        if (m_live)
        {
            return;
        }

        m_engine = &eng;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        m_ini_path = core::os::path_to_utf8(platform::pref_path("AlphaEngine", "AlphaEngine") / "imgui.ini");
        io.IniFilename = m_ini_path.c_str();
        ImGui::StyleColorsDark();

        if (!init_backends())
        {
            ImGui::DestroyContext();
            return;
        }

        m_live = true;

        // Every OS event reaches the overlay before the engine; while a
        // focused panel captures the keyboard or the mouse, the window
        // withholds that input class from every other listener.
        eng.window->set_event_filter(
            [this](const void* native_event)
            {
                process_event(native_event);
                return platform::input_capture{wants_keyboard(), wants_mouse()};
            });

        LOG_INF("editor: ImGui overlay initialised (SDL3 + Vulkan)");
    }

    // Brings up the SDL3 platform backend on the live window and the GPU
    // device's overlay renderer, which draws into the debug pass's
    // swapchain render pass. The window is Vulkan-backed, which the
    // platform backend needs to know for any window of its own it
    // creates.
    bool editor_layer::init_backends()
    {
        if (!ImGui_ImplSDL3_InitForVulkan(m_engine->window->sdl_window()))
        {
            LOG_ERR("editor: ImGui_ImplSDL3_InitForVulkan failed");
            return false;
        }

        m_overlay = m_engine->gpu->create_overlay_renderer();
        if (m_overlay == nullptr)
        {
            LOG_ERR("editor: the GPU device has no overlay renderer for ImGui");
            ImGui_ImplSDL3_Shutdown();
            return false;
        }
        if (!m_overlay->init())
        {
            m_overlay.reset();
            ImGui_ImplSDL3_Shutdown();
            return false;
        }

        m_engine->renderer->set_overlay(m_overlay.get());
        return true;
    }

    void editor_layer::shutdown()
    {
        if (!m_live)
        {
            return;
        }
        // The window stops routing events here before the context they
        // feed goes.
        m_engine->window->set_event_filter(nullptr);

        // The debug pass stops recording the overlay before its renderer
        // releases its GPU resources, which waits for every frame in
        // flight that could still read them.
        m_engine->renderer->set_overlay(nullptr);
        m_overlay->shutdown();
        m_overlay.reset();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_live = false;
        m_selected_node = nullptr;
        LOG_INF("editor: ImGui overlay shut down");
    }

    void editor_layer::process_event(const void* native_event)
    {
        if (!m_live || native_event == nullptr)
        {
            return;
        }
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(native_event));
    }

    bool editor_layer::wants_keyboard() const
    {
        return m_live && ImGui::GetIO().WantCaptureKeyboard;
    }

    bool editor_layer::wants_mouse() const
    {
        return m_live && ImGui::GetIO().WantCaptureMouse;
    }

    void editor_layer::begin_frame()
    {
        if (!m_live)
        {
            return;
        }

        m_overlay->new_frame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        build_panels();
    }

    void editor_layer::end_frame()
    {
        if (!m_live)
        {
            return;
        }

        draw_debug_visuals();
        draw_debug_text();

        ImGui::Render();
    }

    // Hosts a full-viewport, passthrough dockspace so every panel can
    // dock against the window edges and against each other. Built
    // once with a stable id so ImGui's own ini persistence
    // (platform::pref_path, set up in init) remembers whatever
    // arrangement the user leaves it in across runs. Returns the
    // dockspace id so the caller can locate the central node — the
    // passthrough game view the transform gizmo draws over.
    ImGuiID editor_layer::setup_dockspace()
    {
        const ImGuiID dockspace_id = ImGui::GetID("AlphaEngineDockSpace");
        if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
        {
            build_default_dock_layout(dockspace_id);
        }
        ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        return dockspace_id;
    }

    void editor_layer::build_panels()
    {
        // Push this frame's real time, in milliseconds, into the rolling
        // history first so the profiler reflects the live cadence, paused
        // or not.
        const auto& time = *m_engine->time;
        m_profiler.frame_times[m_profiler.frame_cursor] = static_cast<float>(time.unscaled_delta_time() * 1000.0);
        m_profiler.frame_cursor = (m_profiler.frame_cursor + 1) % profiler_state::frame_history;

        const ImGuiID dockspace_id = setup_dockspace();
        validate_selection();
        handle_gizmo_shortcuts();

        draw_fps_overlay();
        draw_render_targets_window();
        draw_console_window();
        draw_hierarchy_window();
        draw_inspector_window();
        draw_profiler_window();
        draw_scene_window();
        draw_settings_window();
        draw_post_window();
        draw_helpers_window();
        draw_gizmo(dockspace_id);
        if (m_show.demo)
        {
            ImGui::ShowDemoWindow(&m_show.demo);
        }
    }

    std::unique_ptr<runtime::overlay> create_overlay()
    {
        return std::make_unique<editor_layer>();
    }
} // namespace editor

#endif // ALPHAENGINE_HAS_IMGUI
