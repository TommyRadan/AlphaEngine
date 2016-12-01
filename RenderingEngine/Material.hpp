#pragma once

#include <string>
#include <map>

#include <Mathematics/Math.hpp>

#include <Utilities/Color.hpp>

#include "OpenGL/Texture.hpp"

struct Material
{
	std::map<std::string, float> Coefficients;
	std::map<std::string, OpenGL::Texture*> Textures;
	std::map<std::string, Color> Colors;
};
