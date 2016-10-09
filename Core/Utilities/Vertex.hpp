#pragma once

#include <Mathematics\Math.hpp>
#include <vector>

struct Vertex
{
	Math::Vector3 Pos;
	Math::Vector2 Tex;
	Math::Vector3 Normal;
};

typedef std::vector<Vertex> VertexVector;
