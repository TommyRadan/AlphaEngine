#pragma once

#include <string>
#include <map>

#include <Utilities/Color.hpp>

struct Material
{
	std::map<std::string, float> Coefficients;
	std::map<std::string, void*> Textures;
	std::map<std::string, Color> Colors;
};
