#include "Geometry.hpp"

Geometry::Geometry(const Mesh& mesh)
{
	m_DrawCount = (unsigned) mesh.VertexCount();

	// Data to the GPU
	m_VertexBufferObject.Data(mesh.Vertices(), mesh.VertexCount() * sizeof(Vertex), OpenGL::BufferUsage::StaticDraw);

	// Position mapping
	m_VertexArrayObject.BindAttribute(0, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), 0U);
	
	// Texture Coordinates mapping
	m_VertexArrayObject.BindAttribute(1, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::Vector3));
	
	// Normals mapping
	m_VertexArrayObject.BindAttribute(2, m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::Vector3) + sizeof(Math::Vector2));
}

void Geometry::Draw(void)
{
	OpenGL::OGL* context = OpenGL::OGL::GetInstance();
	context->DrawArrays(m_VertexArrayObject, OpenGL::Primitive::Triangles, 0U, m_DrawCount);
}
