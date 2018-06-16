#pragma once

#include <Mathematics/glm.hpp>
#include <RenderingEngine/RenderOptions.hpp>

namespace RenderingEngine
{
    struct Renderable
    {
		virtual void Render() = 0;
		RenderOptions options;
    };
}
