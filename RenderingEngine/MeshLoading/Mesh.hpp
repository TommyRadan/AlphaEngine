#pragma once

#include "Vertex.hpp"

#include <vector>

class Mesh
{
public:
	Mesh(void) = default;

	void UploadOBJ(const std::vector<Vertex>& v);

	const Vertex* Vertices(void) const;
	std::size_t VertexCount(void) const;

private:
	std::vector<Vertex> m_Vertices;
};
