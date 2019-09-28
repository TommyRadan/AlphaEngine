/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <exception>

#include <RenderingEngine/Window.hpp>
#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <SDL.h>
#include <Infrastructure/Settings.hpp>
#include <Infrastructure/Color.hpp>
#include <Infrastructure/Log.hpp>

namespace RenderingEngine
{
    Window::Window() :
            m_Window { nullptr },
            m_GlContext { nullptr }
    {}

    Window* Window::GetInstance()
    {
        static Window* instance { nullptr };

        if (instance == nullptr)
        {
            instance = new Window();
        }

        return instance;
    }

    void Window::Init()
    {
        LOG_INFO("Init RenderingEngine::Window");

        auto settings { Settings::GetInstance() };
        uint32_t window_flags { SDL_WINDOW_OPENGL };
        auto type { settings->GetWindowType() };

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
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, settings->IsDoubleBuffered());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        m_Window = SDL_CreateWindow(
                settings->GetWindowName(),
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                settings->GetWindowWidth(),
                settings->GetWindowHeight(),
                window_flags
        );

        if (m_Window == nullptr)
        {
            LOG_FATAL("Cannot create window");
            LOG_FATAL("SDL Error: %s", SDL_GetError());
            throw std::runtime_error { SDL_GetError() };
        }

        m_GlContext = SDL_GL_CreateContext((SDL_Window*) m_Window);
    }

    void Window::Quit()
    {
        SDL_GL_DeleteContext(m_GlContext);
        SDL_DestroyWindow((SDL_Window*) m_Window);

        LOG_INFO("Quit RenderingEngine::Window");
    }

    void Window::Clear()
    {
        OpenGL::Context::GetInstance()->ClearColor(Infrastructure::Color(0, 0, 0, 255));
        OpenGL::Context::GetInstance()->Clear(OpenGL::Buffer::Color);
        OpenGL::Context::GetInstance()->Clear(OpenGL::Buffer::Depth);
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
}
