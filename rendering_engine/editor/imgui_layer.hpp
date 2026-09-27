// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file imgui_layer.hpp
 * @brief Debug-only Dear ImGui overlay (FPS, settings inspector, demo).
 *
 * The whole layer is a thin, header-stable wrapper around ImGui, its
 * SDL3 platform backend and the GPU device's overlay renderer
 * (@ref gpu::overlay_renderer). It is compiled into every configuration but
 * only does real work when @c ALPHAENGINE_HAS_IMGUI is defined — that
 * macro is set for Debug builds only (see the root CMakeLists.txt), so in
 * release every function below collapses to an empty no-op and ImGui is
 * not linked at all. None of these declarations expose an ImGui or SDL
 * type, so callers in the always-compiled engine core (engine, renderer,
 * debug pass) can include this header unconditionally.
 *
 * The frame is split in two: building the panels (@ref begin_frame) runs
 * on the main thread before the passes and ends in @c ImGui::Render,
 * which leaves CPU-side draw data behind; recording that draw data is
 * pure GPU work the debug pass performs itself, through the overlay
 * renderer @ref init hands the renderer, inside its still-open swapchain
 * render pass, without going through the event bus.
 */

#pragma once

namespace rendering_engine::editor
{
    /**
     * @brief Brings ImGui, its SDL3 platform backend and the GPU device's
     *        overlay renderer up against the live window and device, hands
     *        the renderer the overlay renderer for the debug pass to
     *        record, and installs the window's event filter
     *        (@c platform::window::set_event_filter), which hands every
     *        event to @ref process_event and reports @ref wants_keyboard /
     *        @ref wants_mouse as the input it captures. Call once after the
     *        window, GPU device and passes are initialised. No-op in
     *        release.
     */
    void init();

    /**
     * @brief Removes the window's event filter, takes the overlay renderer
     *        back from the renderer and tears ImGui and its backends down.
     *        No-op in release.
     */
    void shutdown();

    /**
     * @brief Forwards a single @c SDL_Event (as an opaque pointer) to the
     *        ImGui SDL3 backend so the overlay receives input. No-op in
     *        release or before @ref init.
     */
    void process_event(const void* sdl_event);

    /**
     * @brief Opens a new ImGui frame and builds the debug panels.
     *
     * Runs the backend / platform new-frame, builds the overlay (FPS,
     * frame-time profiler, settings inspector) and calls @c ImGui::Render
     * so the draw data is ready. Must be called once per frame *before*
     * the rendering passes run, since the draw data is consumed inside the
     * debug pass by the overlay renderer. No-op in release.
     */
    void begin_frame();

    /** @brief True when a focused panel is capturing keyboard input. */
    bool wants_keyboard();

    /** @brief True when a hovered / focused panel is capturing the mouse. */
    bool wants_mouse();
} // namespace rendering_engine::editor
