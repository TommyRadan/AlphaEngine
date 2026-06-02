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

/**
 * @file imgui_layer.hpp
 * @brief Debug-only Dear ImGui overlay (FPS, settings inspector, demo).
 *
 * The whole layer is a thin, header-stable wrapper around ImGui and its
 * SDL3 + OpenGL3 backends. It is compiled into every configuration but
 * only does real work when @c ALPHAENGINE_HAS_IMGUI is defined — that
 * macro is set for Debug builds only (see the root CMakeLists.txt), so in
 * release every function below collapses to an empty no-op and ImGui is
 * not linked at all. None of these declarations expose an ImGui or SDL
 * type, so callers in the always-compiled engine core (window, rendering
 * context) can include this header unconditionally.
 *
 * ImGui renders through the OpenGL3 or the Vulkan backend, matching the
 * GPU backend the engine brought up. Either way the draw data is recorded
 * into the swapchain-targeted debug pass: the layer subscribes to
 * @ref core::render_debug in @ref init and records there, inside
 * the still-open render pass, so the same injection point works for both
 * the immediate-mode OpenGL backend and Vulkan's recorded command buffer.
 */

#pragma once

namespace rendering_engine::debug_ui
{
    /**
     * @brief Brings ImGui and its SDL3 + OpenGL3 / Vulkan backends up
     *        against the live window and GPU device, and subscribes to
     *        @ref core::render_debug so the overlay is recorded
     *        into the debug pass. Call once after the window, GPU device
     *        and passes are initialised. No-op in release.
     */
    void init();

    /** @brief Tears ImGui and its backends down. No-op in release. */
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
     * debug pass via @ref core::render_debug. No-op in release.
     */
    void begin_frame();

    /** @brief True when a focused panel is capturing keyboard input. */
    bool wants_keyboard();

    /** @brief True when a hovered / focused panel is capturing the mouse. */
    bool wants_mouse();
} // namespace rendering_engine::debug_ui
