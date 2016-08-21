#include "Geometry.hpp"

Geometry::Geometry(Mesh& mesh)
{
	m_DrawCount = (unsigned) mesh.VertexCount();

	// Data to the GPU
	m_VertexBufferObject.Data(mesh.Vertices(), mesh.VertexCount() * sizeof(Vertex), OpenGL::BufferUsage::StaticDraw);

	// Position mapping
	m_VertexArrayObject.BindAttribute(0, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), 0U);
	
	// Texture Coordinates mapping
	m_VertexArrayObject.BindAttribute(1, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::vec3));
	
	// Normals mapping
	m_VertexArrayObject.BindAttribute(2, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::vec3) + sizeof(Math::vec2));
}

void Geometry::Draw(void)
{
	OpenGL::Context* context = OpenGL::Context::GetInstance();
	context->DrawArrays(m_VertexArrayObject, OpenGL::Primitive::Triangles, 0U, m_DrawCount);
}
