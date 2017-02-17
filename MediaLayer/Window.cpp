#include "Window.hpp"
#include <SDL2/SDL.h>

namespace MediaLayer
{
    Window::Window() :
        m_Settings { Settings::GetInstance() },
        m_Window { nullptr }
    {}

    void Window::Init(void)
    {
        uint32_t window_flags = SDL_WINDOW_OPENGL;

        WinType type = m_Settings->GetWindowType();
        if(type == WinType::WIN_TYPE_BORDERLESS)
            window_flags |= SDL_WINDOW_BORDERLESS;
        if(type == WinType::WIN_TYPE_FULLSCREEN)
            window_flags |= SDL_WINDOW_FULLSCREEN;

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, m_Settings->IsDoubleBuffered()?1:0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        m_Window = SDL_CreateWindow(
                m_Settings->GetWindowName(),
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                m_Settings->GetWindowWidth(),
                m_Settings->GetWindowHeight(),
                window_flags
        );

        if (m_Window == nullptr) {
            throw Exception(SDL_GetError());
        }

        m_GlContext = new SDL_GLContext;
        *((SDL_GLContext*)m_GlContext) = SDL_GL_CreateContext((SDL_Window*)m_Window);
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }

    void Window::Quit(void)
    {
        SDL_GL_DeleteContext(*((SDL_GLContext*)m_GlContext));
        SDL_DestroyWindow((SDL_Window*)m_Window);
        delete m_GlContext;
    }

    void Window::SwapBuffers(void)
    {
        SDL_GL_SwapWindow((SDL_Window*)m_Window);
    }
}
