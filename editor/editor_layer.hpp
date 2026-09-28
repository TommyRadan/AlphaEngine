// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file editor_layer.hpp
 * @brief @ref editor::editor_layer, the editor object: the ImGui context,
 *        its backends and the state of every panel.
 *
 * Internal to the editor module and included only by its translation
 * units, which compile only with @c ALPHAENGINE_HAS_IMGUI (it names ImGui
 * and ImGuizmo types). The core — context, backends, event filter,
 * dockspace and frame — is in @c editor.cpp; each panel's drawing is in
 * a file of its own, as the member list below groups it.
 */

#pragma once

#include <array>
#include <memory>
#include <string>

#include <imgui.h>
#include <ImGuizmo.h>

#include <core/log.hpp>
#include <runtime/overlay.hpp>

namespace rendering_engine::gpu
{
    struct overlay_renderer;
}

namespace runtime
{
    struct engine;
    struct node;
} // namespace runtime

namespace editor
{
    /** @brief Which of the optional panels are open; toggled from the FPS overlay's context menu. */
    struct panel_visibility
    {
        bool settings{true};
        bool profiler{true};
        bool demo{false};
        bool helpers{true};
        bool scene{true};
        bool post{true};
        bool render_targets{true};
        bool console{true};
        bool hierarchy{true};
        bool inspector{true};
    };

    /**
     * @brief The transform gizmo's mode, shared between the Inspector's
     *        controls, the W/E/R shortcuts and the gizmo drawn over the
     *        viewport, so all three agree on what is shown.
     */
    struct gizmo_state
    {
        ImGuizmo::OPERATION operation{ImGuizmo::TRANSLATE};
        ImGuizmo::MODE mode{ImGuizmo::WORLD};
        bool snap_enabled{false};
        float snap_translate{1.0f};
        float snap_rotate_degrees{15.0f};
        float snap_scale{0.1f};
    };

    /**
     * @brief Which built-in visualisations the editor draws each frame
     *        through the debug-draw functions; toggled from the Helpers
     *        panel. The ground grid's toggle is the grid's own visibility
     *        (@c renderer::editor_grid).
     */
    struct debug_visuals
    {
        // The world axes at the origin.
        bool axes{true};
        // The physics world's colliders and contacts.
        bool physics{true};
        // A gizmo per enabled directional, point and spot light.
        bool lights{false};
        // The view frustum of every enabled camera but the one rendering.
        bool cameras{false};
    };

    /** @brief The Console panel's filters. */
    struct console_state
    {
        // Minimum level shown, as a core::logging::verbosity value.
        int level_filter{static_cast<int>(core::logging::verbosity::trace)};
        // Only records whose category contains this text are shown.
        std::array<char, 64> category_filter{};
        bool auto_scroll{true};
    };

    /** @brief The Inspector panel's name field. */
    struct inspector_state
    {
        // The node whose name name_buffer was filled from; the buffer is
        // refilled when the selection changes.
        runtime::node* name_node{nullptr};
        std::array<char, 128> name_buffer{};
    };

    /** @brief The Profiler panel's rolling frame-time history. */
    struct profiler_state
    {
        static constexpr int frame_history = 120;
        // Milliseconds per frame, used as a ring buffer.
        std::array<float, frame_history> frame_times{};
        int frame_cursor{0};
    };

    /**
     * @brief Edit buffer of the Post panel's colour-grading LUT path. It
     *        mirrors the live path while the field is not being edited,
     *        and an edit is applied when committed with Enter.
     */
    struct post_panel_state
    {
        std::array<char, 512> grading_lut_input{};
        bool grading_lut_editing{false};
    };

    /**
     * @brief The debug editor, as the overlay the engine drives.
     *
     * @ref init creates the ImGui context, brings up the SDL3 platform
     * backend and the GPU device's overlay renderer (which the renderer's
     * debug pass then records every frame), and installs the window's
     * event filter; @ref begin_frame starts the ImGui frame and builds
     * every panel; @ref end_frame, once the render extraction has written
     * the frame's proxies, draws the debug visualisations and the
     * debug-draw text labels and ends in @c ImGui::Render; @ref shutdown
     * undoes @ref init in reverse. Every piece of panel state lives in this
     * object.
     */
    struct editor_layer final : runtime::overlay
    {
        editor_layer() = default;
        ~editor_layer() override;

        editor_layer(const editor_layer&) = delete;
        editor_layer& operator=(const editor_layer&) = delete;

        void init(runtime::engine& eng) override;
        void begin_frame() override;
        void end_frame() override;
        void shutdown() override;

    private:
        // -- Core (editor.cpp) --------------------------------------------

        // Brings the SDL3 platform backend and the device's overlay
        // renderer up; false, with the reason logged, when either failed.
        bool init_backends();
        // Forwards one SDL_Event to the SDL3 platform backend.
        void process_event(const void* native_event);
        // True while a focused panel captures the keyboard / mouse.
        bool wants_keyboard() const;
        bool wants_mouse() const;
        // Hosts the full-viewport dockspace every panel docks into, and
        // returns its id (the gizmo draws over its central node).
        ImGuiID setup_dockspace();
        void build_panels();

        // -- Panels, one file each ----------------------------------------

        // fps_overlay.cpp
        void draw_fps_overlay();

        // hierarchy_panel.cpp
        void validate_selection();
        void draw_node_row(runtime::node& node);
        void draw_hierarchy_window();

        // inspector_panel.cpp
        void draw_inspector_window();

        // gizmo.cpp
        void draw_gizmo_controls();
        void handle_gizmo_shortcuts();
        void draw_gizmo(ImGuiID dockspace_id);

        // console_panel.cpp
        void draw_console_window();

        // render_targets_panel.cpp
        void draw_render_targets_window();

        // profiler_panel.cpp
        void draw_profiler_window();

        // scene_panel.cpp
        void draw_scene_window();

        // settings_panel.cpp
        void draw_settings_window();

        // post_panel.cpp
        void draw_post_window();

        // helpers_panel.cpp
        void draw_helpers_window();

        // debug_visuals.cpp
        void draw_debug_visuals();
        void draw_debug_text();

        // -- State --------------------------------------------------------

        // The engine init was handed; null before init.
        runtime::engine* m_engine{nullptr};

        // Set once init succeeds, so every entry point early-outs when
        // ImGui is not live.
        bool m_live{false};

        // The GPU backend's half of the overlay: it opens each frame,
        // registers the render-target viewer's textures and records the
        // draw data inside the debug pass (see renderer::set_overlay).
        // Live exactly while m_live is set.
        std::unique_ptr<rendering_engine::gpu::overlay_renderer> m_overlay;

        // Where ImGui persists window / dock layout (imgui.ini): the
        // per-user preference directory rather than beside the
        // executable, matching the shader cache's use of the same pref
        // path. ImGui stores io.IniFilename as a raw pointer rather than
        // copying it, so the backing string must outlive the context;
        // this object does.
        std::string m_ini_path;

        panel_visibility m_show;

        // The scene-graph node currently selected in the Hierarchy panel
        // and shown by the Inspector panel, or null. Validated once per
        // frame (see validate_selection) against the live scene forest
        // before the Inspector dereferences it, so a node destroyed since
        // it was selected is never read.
        runtime::node* m_selected_node{nullptr};

        gizmo_state m_gizmo;
        console_state m_console;
        inspector_state m_inspector;
        profiler_state m_profiler;
        post_panel_state m_post;
        debug_visuals m_visuals;

        // The texture the Render Targets panel shows, as an index into
        // its slot list.
        int m_render_target_selected{0};
    };
} // namespace editor
