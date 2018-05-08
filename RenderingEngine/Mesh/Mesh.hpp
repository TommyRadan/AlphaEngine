#pragma once

#include <RenderingEngine/Mesh/Vertex.hpp>

#include <vector>

namespace RenderingEngine
{
	struct Mesh
	{
		void UploadOBJ(const std::vector<Vertex> &v);

		const Vertex* Vertices() const;
		std::size_t VertexCount() const;

	private:
		std::vector<Vertex> m_Vertices;
	};
}
