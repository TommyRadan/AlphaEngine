/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file window.hpp
 * @brief SDL-backed OS window and GL context owner; source of input events.
 */

#pragma once

#include <memory>
#include <string>

#include <infrastructure/singleton.hpp>

struct SDL_Window;

namespace rendering_engine
{
    /** @brief Custom deleter that destroys an @c SDL_Window via @c SDL_DestroyWindow. */
    struct sdl_window_deleter
    {
        void operator()(SDL_Window* w) const noexcept;
    };

    /** @brief Custom deleter that destroys an SDL GL context via @c SDL_GL_DeleteContext. */
    struct sdl_gl_context_deleter
    {
        void operator()(void* ctx) const noexcept;
    };

    /** @brief RAII-owning handle to an @c SDL_Window. */
    using sdl_window_handle = std::unique_ptr<SDL_Window, sdl_window_deleter>;

    /** @brief RAII-owning handle to an SDL OpenGL context. */
    using sdl_gl_context_handle = std::unique_ptr<void, sdl_gl_context_deleter>;

    /**
     * @brief Owns the application window, its GL context, and pumps OS input.
     *
     * Process-wide singleton. The window and GL context are held as
     * @c std::unique_ptr with SDL-specific deleters, so their lifetime
     * is strictly tied to this instance — @ref init creates them and
     * @ref quit (or destruction) releases them. All methods must be
     * invoked from the main thread.
     */
    struct window : public singleton<window>
    {
        window();

        /**
         * @brief Initializes SDL video, creates the window and the GL
         *        context using the dimensions and flags read from the
         *        global @c settings singleton.
         * @throws std::runtime_error if SDL video init or window creation fails.
         */
        void init();

        /** @brief Destroys the GL context and window and shuts down SDL video. */
        void quit();

        /**
         * @brief Pumps the SDL event queue and translates OS input into
         *        engine events broadcast through @ref event_engine::event_bus.
         *
         * Also broadcasts a @ref event_engine::frame event carrying the
         * current delta time. Call once per main-loop iteration.
         */
        void tick();

        /** @brief Clears the color and depth buffers to opaque black. */
        void clear();

        /** @brief Presents the back buffer to the screen. */
        void swap_buffers();

        /**
         * @brief Displays a modal error message box parented to the window.
         * @param title   Title of the message box.
         * @param message Body text of the message box.
         */
        void show_message(const std::string& title, const std::string& message);

        /** @brief Shows the OS cursor and disables relative mouse mode. */
        void show_cursor();

        /** @brief Hides the OS cursor and enables relative mouse mode. */
        void hide_cursor();

    private:
        sdl_window_handle m_window;
        sdl_gl_context_handle m_gl_context;
    };
} // namespace rendering_engine
