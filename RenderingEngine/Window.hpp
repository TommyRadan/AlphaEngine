#pragma once

#include <string>

namespace RenderingEngine
{
    class Window
    {
        Window();

    public:
        static Window* GetInstance();

        void Init();
        void Quit();

        void Clear();
        void SwapBuffers();
        void ShowMessage(const std::string& title, const std::string& message);

    private:
        void* m_Window;
        void* m_GlContext;
    };
}
