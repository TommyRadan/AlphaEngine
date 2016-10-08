#pragma once

#include <Utilities\Exception.hpp>
#include <Mathematics\Math.hpp>

#include <fstream>
#include <vector>

struct Vertex
{
	Math::Vector3 Pos;
	Math::Vector2 Tex;
	Math::Vector3 Normal;
};

class Mesh
{
public:
	Mesh(const std::string& filename);

	const Vertex* Vertices(void) const;
	std::size_t VertexCount(void) const;

private:
	std::vector<Vertex> vertices;
};
