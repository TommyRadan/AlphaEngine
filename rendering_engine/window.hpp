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

#pragma once

#include <memory>
#include <string>

#include <infrastructure/singleton.hpp>

struct SDL_Window;

namespace rendering_engine
{
    struct sdl_window_deleter
    {
        void operator()(SDL_Window* w) const noexcept;
    };

    struct sdl_gl_context_deleter
    {
        void operator()(void* ctx) const noexcept;
    };

    using sdl_window_handle = std::unique_ptr<SDL_Window, sdl_window_deleter>;
    using sdl_gl_context_handle = std::unique_ptr<void, sdl_gl_context_deleter>;

    struct window : public singleton<window>
    {
        window();

        void init();
        void quit();

        void tick();

        void clear();
        void swap_buffers();
        void show_message(const std::string& title, const std::string& message);

        void show_cursor();
        void hide_cursor();

    private:
        sdl_window_handle m_window;
        sdl_gl_context_handle m_gl_context;
    };
} // namespace rendering_engine
