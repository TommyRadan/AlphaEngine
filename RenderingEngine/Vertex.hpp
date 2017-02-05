#pragma once

#include <Mathematics/Math.hpp>

#include <vector>
#include <string>
#include <utility>

struct Vertex
{
	Math::Vector3 Pos;
	Math::Vector2 Tex;
	Math::Vector3 Normal;
};

using VertexVector = std::vector<Vertex>;
using VertexFormat = std::vector<std::pair<std::string, unsigned int>>;
