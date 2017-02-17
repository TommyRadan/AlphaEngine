#pragma once

#include <Control/Settings.hpp>
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
        Settings* m_Settings;

        // SDL_Window
        void* m_Window;

        // SDL Gl Context
        void* m_GlContext;
    };
}
