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

typedef std::vector<Vertex> VertexVector;
typedef std::vector<std::pair<std::string, unsigned int> > VertexFormat;
