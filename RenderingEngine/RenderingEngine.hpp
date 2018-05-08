#pragma once

#include <Infrastructure/Subsystem.hpp>
#include <RenderingEngine/Renderables/Renderable.hpp>
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
    };
}
