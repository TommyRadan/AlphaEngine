#include "Mesh.hpp"

Mesh::Mesh(void)
{}

void Mesh::UploadOBJ(const VertexVector& v)
{
	m_Vertices = std::move(v);
}

const Vertex* Mesh::Vertices(void) const
{
	return &m_Vertices[0];
}

std::size_t Mesh::VertexCount(void) const
{
	return m_Vertices.size();
}