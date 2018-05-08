#include <RenderingEngine/Mesh/Mesh.hpp>

void RenderingEngine::Mesh::UploadOBJ(const std::vector<Vertex>& v)
{
	m_Vertices = v;
}

const RenderingEngine::Vertex* RenderingEngine::Mesh::Vertices() const
{
	return &m_Vertices[0];
}

std::size_t RenderingEngine::Mesh::VertexCount() const
{
	return m_Vertices.size();
}
