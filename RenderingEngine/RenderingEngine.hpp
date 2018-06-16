#pragma once

#include <Infrastructure/Subsystem.hpp>
#include <RenderingEngine/Renderer.hpp>
#include <vector>

namespace RenderingEngine
{
    class Context : public Infrastructure::Subsystem
    {
        Context() = default;

    public:
        static Context* GetInstance();

        void Init() final;
        void Quit() final;

        void Render();

        Renderer* const GetCurrentRenderer();

    private:
        friend Renderer;
        Renderer* m_CurrentRenderer;
    };
}
