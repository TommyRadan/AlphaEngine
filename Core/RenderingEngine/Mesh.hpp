#pragma once

#include <Utilities\Exceptions\Exception.hpp>
#include <Mathematics\Math.hpp>

#include <fstream>
#include <vector>

struct Vertex
{
	Math::vec3 Pos;
	Math::vec2 Tex;
	Math::vec3 Normal;
};

class Mesh
{
public:
	Mesh(const std::string& filename);

	const Vertex* Vertices() const;
	std::size_t VertexCount() const;

private:
	std::vector<Vertex> vertices;
};
