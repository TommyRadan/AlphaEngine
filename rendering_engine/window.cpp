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

#include <stdexcept>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace rendering_engine
{
    void sdl_window_deleter::operator()(SDL_Window* w) const noexcept
    {
        if (w != nullptr)
        {
            SDL_DestroyWindow(w);
        }
    }

    void sdl_gl_context_deleter::operator()(void* ctx) const noexcept
    {
        if (ctx != nullptr)
        {
            SDL_GL_DestroyContext(static_cast<SDL_GLContext>(ctx));
        }
    }

    window::window() = default;

    void window::init()
    {
        LOG_INF("Init rendering_engine::window");

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            LOG_FTL("Could not initialize video system");
            LOG_FTL("SDL_Error: %s", SDL_GetError());
            throw std::runtime_error{"Could not initialize video system"};
        }

        ::settings& s{*runtime::current_engine().settings};
        m_is_vulkan = s.graphics.backend == graphics_backend::vulkan;
        SDL_WindowFlags window_flags{m_is_vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL};
        auto type{s.window.type};

        const char* type_name = "windowed";
        bool fullscreen = false;
        if (type == win_type::win_type_borderless)
        {
            window_flags |= SDL_WINDOW_BORDERLESS;
            type_name = "borderless";
        }

        if (type == win_type::win_type_fullscreen)
        {
            window_flags |= SDL_WINDOW_FULLSCREEN;
            type_name = "fullscreen";
            fullscreen = true;
        }

        LOG_INF("Creating window: name='%s' size=%ux%u mode=%s double_buffered=%s",
                s.window.name.c_str(),
                s.window.width,
                s.window.height,
                type_name,
                s.window.double_buffered ? "true" : "false");

        if (!m_is_vulkan)
        {
            SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, s.window.double_buffered);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        }

        m_window.reset(SDL_CreateWindow(s.window.name.c_str(), s.window.width, s.window.height, window_flags));

        if (m_window == nullptr)
        {
            LOG_FTL("Cannot create window");
            LOG_FTL("SDL Error: %s", SDL_GetError());
            throw std::runtime_error{SDL_GetError()};
        }

        if (fullscreen)
        {
            SDL_HideCursor();
            SDL_SetWindowRelativeMouseMode(m_window.get(), true);
        }

        if (m_is_vulkan)
        {
            // Vulkan owns presentation through vkQueuePresentKHR; the
            // window stays an SDL_WINDOW_VULKAN container only.
            LOG_INF("SDL window created (Vulkan presentation)");
        }
        else
        {
            m_gl_context.reset(SDL_GL_CreateContext(m_window.get()));
            if (m_gl_context == nullptr)
            {
                LOG_FTL("Could not create SDL GL context: %s", SDL_GetError());
                throw std::runtime_error{SDL_GetError()};
            }
            LOG_INF("SDL window and GL context created successfully");
            const int swap_interval = s.window.vsync ? 1 : 0;
            if (!SDL_GL_SetSwapInterval(swap_interval))
            {
                LOG_WRN("Could not set swap interval to %d: %s", swap_interval, SDL_GetError());
            }
        }
    }

    void window::quit()
    {
        m_gl_context.reset();
        m_window.reset();

        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        LOG_INF("Quit rendering_engine::window");
    }

    void window::swap_buffers()
    {
        // Vulkan presents through vkQueuePresentKHR inside
        // gpu::device::submit; nothing to do here on that path.
        if (!m_is_vulkan)
        {
            SDL_GL_SwapWindow(m_window.get());
        }
    }

    SDL_Window* window::sdl_window() const noexcept
    {
        return m_window.get();
    }

    void* window::gl_context() const noexcept
    {
        return m_gl_context.get();
    }

    void window::show_message(const std::string& title, const std::string& message)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), m_window.get());
    }

    void window::show_cursor()
    {
        SDL_ShowCursor();
        SDL_SetWindowRelativeMouseMode(m_window.get(), false);
    }

    void window::hide_cursor()
    {
        SDL_HideCursor();
        SDL_SetWindowRelativeMouseMode(m_window.get(), true);
    }

    void window::tick()
    {
        SDL_Event events = {0};

        auto& eng = runtime::current_engine();
        auto& bus = *eng.events;

        core::frame frame;
        frame.m_delta_time = static_cast<float>(eng.time->delta_time());
        bus.emit<core::frame>(frame);

        SDL_PumpEvents();
        while (SDL_PollEvent(&events))
        {
            // Let the debug UI see every event first. When a debug panel
            // has focus it captures the matching input class so the same
            // click / keystroke does not also drive the camera or game
            // modules. Both calls are no-ops in release builds.
            debug_ui::process_event(&events);
            const bool ui_wants_keyboard = debug_ui::wants_keyboard();
            const bool ui_wants_mouse = debug_ui::wants_mouse();

            switch (events.type)
            {
            case SDL_EVENT_KEY_DOWN:
            {
                if (ui_wants_keyboard)
                {
                    break;
                }
                core::key_down key_down;
                key_down.m_key_code = (core::key_code)events.key.key;
                bus.emit<core::key_down>(key_down);
                break;
            }

            case SDL_EVENT_KEY_UP:
            {
                if (ui_wants_keyboard)
                {
                    break;
                }
                core::key_up key_up;
                key_up.m_key_code = (core::key_code)events.key.key;
                bus.emit<core::key_up>(key_up);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                core::mouse_key_down mouse_key_down;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_key_down.m_key_code = core::mouse_key_code::left;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_down.m_key_code = core::mouse_key_code::right;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_down.m_key_code = core::mouse_key_code::middle;
                    break;

                default:
                    break;
                }
                bus.emit<core::mouse_key_down>(mouse_key_down);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                core::mouse_key_up mouse_key_up;
                mouse_key_up.m_key_code = core::mouse_key_code::left;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_key_up.m_key_code = core::mouse_key_code::left;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_up.m_key_code = core::mouse_key_code::right;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_up.m_key_code = core::mouse_key_code::middle;
                    break;

                default:
                    break;
                }
                bus.emit<core::mouse_key_up>(mouse_key_up);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                if (ui_wants_mouse)
                {
                    break;
                }
                core::mouse_move mouse_move;
                mouse_move.m_x = static_cast<int>(events.motion.xrel);
                mouse_move.m_y = static_cast<int>(events.motion.yrel);
                bus.emit<core::mouse_move>(mouse_move);
                break;
            }

            case SDL_EVENT_QUIT:
                bus.emit<core::quit_requested>();
                break;

            default:
                break;
            }
        }
    }
} // namespace rendering_engine
