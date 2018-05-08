#pragma once

#include <string>
#include <map>

#include <Infrastructure/Color.hpp>
#include <RenderingEngine/OpenGL/Texture.hpp>

namespace RenderingEngine
{
    struct RenderOptions
    {
        std::map<std::string, float> Coefficients;
        std::map<std::string, OpenGL::Texture*> Textures;
        std::map<std::string, Infrastructure::Color> Colors;
    };
}
