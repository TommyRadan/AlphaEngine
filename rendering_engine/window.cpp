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

#include <control/engine.hpp>
#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/window.hpp>
#include <SDL3/SDL.h>

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

        ::settings& s{*control::current_engine().settings};
        SDL_WindowFlags window_flags{SDL_WINDOW_OPENGL};
        auto type{s.get_window_type()};

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
                s.get_window_name(),
                s.get_window_width(),
                s.get_window_height(),
                type_name,
                s.is_double_buffered() ? "true" : "false");

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, s.is_double_buffered());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

        m_window.reset(
            SDL_CreateWindow(s.get_window_name(), s.get_window_width(), s.get_window_height(), window_flags));

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

        m_gl_context.reset(SDL_GL_CreateContext(m_window.get()));
        if (m_gl_context == nullptr)
        {
            LOG_FTL("Could not create SDL GL context: %s", SDL_GetError());
            throw std::runtime_error{SDL_GetError()};
        }
        LOG_INF("SDL window and GL context created successfully");
        if (!SDL_GL_SetSwapInterval(1))
        {
            LOG_WRN("Could not enable vsync: %s", SDL_GetError());
        }
    }

    void window::quit()
    {
        m_gl_context.reset();
        m_window.reset();

        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        LOG_INF("Quit rendering_engine::window");
    }

    void window::clear()
    {
        auto& gl = *control::current_engine().opengl;
        gl.clear_color(rendering_engine::util::color{0, 0, 0, 255});
        gl.clear(opengl::buffer::color);
        gl.clear(opengl::buffer::depth);
    }

    void window::swap_buffers()
    {
        SDL_GL_SwapWindow(m_window.get());
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

        auto& eng = control::current_engine();
        auto& bus = *eng.events;

        event_engine::frame frame;
        frame.m_delta_time = static_cast<float>(eng.time->delta_time());
        bus.broadcast(frame);

        SDL_PumpEvents();
        while (SDL_PollEvent(&events))
        {
            switch (events.type)
            {
            case SDL_EVENT_KEY_DOWN:
            {
                event_engine::key_down key_down;
                key_down.m_key_code = (event_engine::key_code)events.key.key;
                bus.broadcast(key_down);
                break;
            }

            case SDL_EVENT_KEY_UP:
            {
                event_engine::key_up key_up;
                key_up.m_key_code = (event_engine::key_code)events.key.key;
                bus.broadcast(key_up);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                event_engine::mouse_key_down mouse_key_down;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::left;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::right;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::middle;
                    break;

                default:
                    break;
                }
                bus.broadcast(mouse_key_down);
                break;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                event_engine::mouse_key_up mouse_key_up;
                mouse_key_up.m_key_code = event_engine::mouse_key_code::left;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_key_up.m_key_code = event_engine::mouse_key_code::left;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_up.m_key_code = event_engine::mouse_key_code::right;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_up.m_key_code = event_engine::mouse_key_code::middle;
                    break;

                default:
                    break;
                }
                bus.broadcast(mouse_key_up);
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            {
                event_engine::mouse_move mouse_move;
                mouse_move.m_x = static_cast<int>(events.motion.xrel);
                mouse_move.m_y = static_cast<int>(events.motion.yrel);
                bus.broadcast(mouse_move);
                break;
            }

            case SDL_EVENT_QUIT:
                bus.broadcast(event_engine::quit_requested());
                break;

            default:
                break;
            }
        }
    }
} // namespace rendering_engine
