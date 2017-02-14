#pragma once

#include <Control/Settings.hpp>
#include <SDL2/SDL.h>
#include <Utilities/Exception.hpp>

namespace MediaLayer
{
    struct Window
    {
        static Window* GetInstance(void)
        {
            static Window* instance = nullptr;
            if(instance == nullptr) {
                instance = new Window();
            }
            return instance;
        }

        void Init(void);
        void Quit(void);

        void SwapBuffers(void);

    private:
        Window(void);
        SDL_Window* m_Window;
        SDL_GLContext m_GlContext;
        Settings* m_Settings;
    };
}
