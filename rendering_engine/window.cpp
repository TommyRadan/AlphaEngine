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

#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/window.hpp>
#include <SDL.h>

namespace rendering_engine
{
    window::window() : m_window{nullptr}, m_gl_context{nullptr} {}

    void window::init()
    {
        LOG_INF("Init rendering_engine::window");

        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            LOG_FTL("Could not initialize video system");
            LOG_FTL("SDL_Error: %s", SDL_GetError());
            throw std::runtime_error{"Could not initialize video system"};
        }

        ::settings& s{::settings::get_instance()};
        uint32_t window_flags{SDL_WINDOW_OPENGL};
        auto type{s.get_window_type()};

        if (type == win_type::win_type_borderless)
        {
            window_flags |= SDL_WINDOW_BORDERLESS;
        }

        if (type == win_type::win_type_fullscreen)
        {
            window_flags |= SDL_WINDOW_FULLSCREEN;
            SDL_ShowCursor(SDL_DISABLE);
            SDL_SetRelativeMouseMode(SDL_TRUE);
        }

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, s.is_double_buffered());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        m_window = SDL_CreateWindow(s.get_window_name(),
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    s.get_window_width(),
                                    s.get_window_height(),
                                    window_flags);

        if (m_window == nullptr)
        {
            LOG_FTL("Cannot create window");
            LOG_FTL("SDL Error: %s", SDL_GetError());
            throw std::runtime_error{SDL_GetError()};
        }

        m_gl_context = SDL_GL_CreateContext(static_cast<SDL_Window*>(m_window));
        SDL_GL_SetSwapInterval(0);
    }

    void window::quit()
    {
        SDL_GL_DeleteContext(m_gl_context);
        SDL_DestroyWindow(static_cast<SDL_Window*>(m_window));

        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        LOG_INF("Quit rendering_engine::window");
    }

    void window::clear()
    {
        opengl::context::get_instance().clear_color(rendering_engine::util::color{0, 0, 0, 255});
        opengl::context::get_instance().clear(opengl::buffer::color);
        opengl::context::get_instance().clear(opengl::buffer::depth);
    }

    void window::swap_buffers()
    {
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(m_window));
    }

    void window::show_message(const std::string& title, const std::string& message)
    {
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), static_cast<SDL_Window*>(m_window));
    }

    void window::show_cursor()
    {
        SDL_ShowCursor(SDL_ENABLE);
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }

    void window::hide_cursor()
    {
        SDL_ShowCursor(SDL_DISABLE);
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }

    void window::tick()
    {
        SDL_Event events = {0};

        event_engine::frame frame;
        frame.m_delta_time = static_cast<float>(infrastructure::time::get_instance().delta_time());
        event_engine::context::get_instance().broadcast(frame);

        SDL_PumpEvents();
        while (SDL_PollEvent(&events))
        {
            switch (events.type)
            {
            case SDL_KEYDOWN:
            {
                event_engine::key_down key_down;
                key_down.m_key_code = (event_engine::key_code)events.key.keysym.sym;
                event_engine::context::get_instance().broadcast(key_down);
                break;
            }

            case SDL_KEYUP:
            {
                event_engine::key_up key_up;
                key_up.m_key_code = (event_engine::key_code)events.key.keysym.sym;
                event_engine::context::get_instance().broadcast(key_up);
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
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
                event_engine::context::get_instance().broadcast(mouse_key_down);
                break;
            }

            case SDL_MOUSEBUTTONUP:
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
                event_engine::context::get_instance().broadcast(mouse_key_up);
                break;
            }

            case SDL_MOUSEMOTION:
            {
                event_engine::mouse_move mouse_move;
                mouse_move.m_x = events.motion.xrel;
                mouse_move.m_y = events.motion.yrel;
                event_engine::context::get_instance().broadcast(mouse_move);
                break;
            }

            case SDL_QUIT:
                event_engine::context::get_instance().broadcast(event_engine::quit_requested());
                break;

            default:
                break;
            }
        }
    }
} // namespace rendering_engine
