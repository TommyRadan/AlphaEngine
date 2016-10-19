#pragma once

#include <Mathematics\Math.hpp>

#include <vector>
#include <string>
#include <map>

struct Vertex
{
	Math::Vector3 Pos;
	Math::Vector2 Tex;
	Math::Vector3 Normal;
};

typedef std::vector<Vertex> VertexVector;
typedef std::map<std::string, size_t> VertexFormat;
