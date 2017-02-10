#pragma once

#include "Vertex.hpp"

#include <vector>

class Mesh
{
public:
	Mesh(void);

	void UploadOBJ(const VertexVector& v);

	const Vertex* Vertices(void) const;
	std::size_t VertexCount(void) const;

private:
	VertexVector m_Vertices;
};
