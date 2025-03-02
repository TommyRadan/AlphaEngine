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
#include <RenderingEngine/Window.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <SDL.h>
#include <Infrastructure/Settings.hpp>
#include <Infrastructure/Time.hpp>
#include <RenderingEngine/Util/Color.hpp>
#include <Infrastructure/Log.hpp>

namespace RenderingEngine
{
    Window::Window() :
            m_Window { nullptr },
            m_GlContext { nullptr }
    {}

    void Window::Init()
    {
        LOG_INFO("Init RenderingEngine::Window");

        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        {
            LOG_FATAL("Could not initialize video system");
            LOG_FATAL("SDL_Error: %s", SDL_GetError());
            throw std::runtime_error {"Could not initialize video system"};
        }

        Settings& settings { Settings::get_instance() };
        uint32_t window_flags { SDL_WINDOW_OPENGL };
        auto type { settings.GetWindowType() };

        if (type == WinType::WIN_TYPE_BORDERLESS)
        {
            window_flags |= SDL_WINDOW_BORDERLESS;
        }

        if (type == WinType::WIN_TYPE_FULLSCREEN)
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
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, settings.IsDoubleBuffered());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        m_Window = SDL_CreateWindow(
                settings.GetWindowName(),
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                settings.GetWindowWidth(),
                settings.GetWindowHeight(),
                window_flags
        );

        if (m_Window == nullptr)
        {
            LOG_FATAL("Cannot create window");
            LOG_FATAL("SDL Error: %s", SDL_GetError());
            throw std::runtime_error { SDL_GetError() };
        }

        m_GlContext = SDL_GL_CreateContext((SDL_Window*) m_Window);
        SDL_GL_SetSwapInterval(0);
    }

    void Window::Quit()
    {
        SDL_GL_DeleteContext(m_GlContext);
        SDL_DestroyWindow((SDL_Window*) m_Window);

        SDL_QuitSubSystem(SDL_INIT_VIDEO);

        LOG_INFO("Quit RenderingEngine::Window");
    }

    void Window::Clear()
    {
        OpenGL::Context::get_instance().ClearColor(RenderingEngine::Util::Color{ 0, 0, 0, 255 });
        OpenGL::Context::get_instance().Clear(OpenGL::Buffer::Color);
        OpenGL::Context::get_instance().Clear(OpenGL::Buffer::Depth);
    }

    void Window::SwapBuffers()
    {
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(m_Window));
    }

    void Window::ShowMessage(const std::string& title, const std::string& message)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                                 title.c_str(),
                                 message.c_str(),
                                 static_cast<SDL_Window*>(m_Window));
    }

    void Window::ShowCursor()
    {
        SDL_ShowCursor(SDL_ENABLE);
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }

    void Window::HideCursor()
    {
        SDL_ShowCursor(SDL_DISABLE);
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }

    void Window::Tick()
    {
        SDL_Event events = { 0 };

        event_engine::frame frame;
        frame.m_delta_time = Infrastructure::Time::get_instance().DeltaTime();
        event_engine::context::get_instance().broadcast(frame);

        SDL_PumpEvents();
        while (SDL_PollEvent(&events))
        {
            switch (events.type)
            {
            case SDL_KEYDOWN: {
                event_engine::key_down key_down;
                key_down.m_key_code = (event_engine::key_code) events.key.keysym.sym;
                event_engine::context::get_instance().broadcast(key_down);
                break;
            }

            case SDL_KEYUP: {
                event_engine::key_up key_up;
                key_up.m_key_code = (event_engine::key_code) events.key.keysym.sym;
                event_engine::context::get_instance().broadcast(key_up);
                break;
            }

            case SDL_MOUSEBUTTONDOWN: {
                event_engine::mouse_key_down mouse_key_down;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::LEFT;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::RIGHT;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_down.m_key_code = event_engine::mouse_key_code::MIDDLE;
                    break;

                default:
                    break;
                }
                event_engine::context::get_instance().broadcast(mouse_key_down);
                break;
            }

            case SDL_MOUSEBUTTONUP: {
                event_engine::mouse_key_up mouse_key_up;
                mouse_key_up.m_key_code = event_engine::mouse_key_code::LEFT;
                switch (events.button.button)
                {
                case SDL_BUTTON_LEFT:
                mouse_key_up.m_key_code = event_engine::mouse_key_code::LEFT;
                    break;

                case SDL_BUTTON_RIGHT:
                    mouse_key_up.m_key_code = event_engine::mouse_key_code::RIGHT;
                    break;

                case SDL_BUTTON_MIDDLE:
                    mouse_key_up.m_key_code = event_engine::mouse_key_code::MIDDLE;
                    break;

                default:
                    break;
                }
                event_engine::context::get_instance().broadcast(mouse_key_up);
                break;
            }

            case SDL_MOUSEMOTION: {
                event_engine::mouse_move mouse_move;
                mouse_move.m_x = events.motion.x;
                mouse_move.m_y = events.motion.y;
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
}
