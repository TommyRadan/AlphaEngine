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
 * ImGui currently renders through the OpenGL3 backend only. When the
 * Vulkan backend is selected the layer initialises to an inert state and
 * logs once; bringing the Vulkan ImGui backend up is tracked as a
 * follow-up (it needs the backend to expose its instance / device / queue
 * / descriptor pool).
 */

#pragma once

namespace rendering_engine::debug_ui
{
    /**
     * @brief Brings ImGui and its SDL3 + OpenGL3 backends up against the
     *        live window and GL context. Call once after the window and
     *        GPU device are initialised. No-op in release.
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
     * @brief Builds and renders the debug overlay for the current frame.
     *
     * Runs a full ImGui frame (new-frame / build panels / render) and
     * paints the draw data onto the default framebuffer, so it must be
     * called after the rendering passes have submitted and before the
     * buffers are swapped. No-op in release.
     */
    void render();

    /** @brief True when a focused panel is capturing keyboard input. */
    bool wants_keyboard();

    /** @brief True when a hovered / focused panel is capturing the mouse. */
    bool wants_mouse();
} // namespace rendering_engine::debug_ui
