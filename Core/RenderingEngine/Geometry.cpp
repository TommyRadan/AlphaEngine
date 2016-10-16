#include "Geometry.hpp"
#include "OpenGL\OpenGL.hpp"
#include "Utilities\Exception.hpp"

Geometry::Geometry(const Mesh& mesh) :
	m_DataUploaded { false }
{}

Geometry::Geometry(const Mesh& mesh) :
	m_DataUploaded { false }
{
	delete m_VertexArrayObject;
	delete m_VertexBufferObject;
}

void Geometry::UploadMesh(const Mesh& mesh)
{
	if(m_DataUploaded) {
		throw Exception("Attempted to call Geometry::UploadMesh on full Geometry");
	}

	m_VertexBufferObject = new VertexBuffer();
	m_VertexArrayObject = new VertexArray();

	m_DrawCount = (unsigned)mesh.VertexCount();
	m_DataUploaded = true;

	// Data to the GPU
	m_VertexBufferObject->Data(mesh.Vertices(), mesh.VertexCount() * sizeof(Vertex), OpenGL::BufferUsage::StaticDraw);

	// Mapping
	m_VertexArrayObject->BindAttribute(0, *m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), 0U);
	m_VertexArrayObject->BindAttribute(1, *m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::Vector3));
	m_VertexArrayObject->BindAttribute(2, *m_VertexBufferObject, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(Math::Vector3) + sizeof(Math::Vector2));
}

void Geometry::ReleaseData(void)
{
	if(!m_DataUploaded) return;

	delete m_VertexArrayObject;
	delete m_VertexBufferObject;
	m_DrawCount = 0u;
	m_DataUploaded = false;
}

void Geometry::Draw(void)
{
	if(!m_DataUploaded) {
		throw Exception("Attempted to call Geometry::Draw on empty Geometry");
	}

	OpenGL::Context* context = OpenGL::Context::GetInstance();
	context->DrawArrays(m_VertexArrayObject, OpenGL::Primitive::Triangles, 0U, m_DrawCount);
}
