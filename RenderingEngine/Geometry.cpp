#include "Geometry.hpp"
#include "Utilities/Exception.hpp"
#include "OpenGL/OpenGL.hpp"

Geometry::Geometry(void) :
	m_DataUploaded { false },
	m_VertexArrayObject { nullptr },
	m_VertexBufferObject { nullptr }
{}

Geometry::~Geometry(void)
{
	delete (OpenGL::VertexArray*)m_VertexArrayObject;
	delete (OpenGL::VertexBuffer*)m_VertexBufferObject;
}

void Geometry::UploadMesh(const Mesh& mesh)
{
	if(m_DataUploaded) {
		throw Exception("Attempted to call Geometry::UploadMesh on full Geometry");
	}

	m_VertexBufferObject = new OpenGL::VertexBuffer();
	m_VertexArrayObject = new OpenGL::VertexArray();

	m_DrawCount = unsigned(mesh.VertexCount());
	m_DataUploaded = true;

	OpenGL::VertexBuffer* vbo = (OpenGL::VertexBuffer*) m_VertexBufferObject;
	OpenGL::VertexArray* vao = (OpenGL::VertexArray*) m_VertexArrayObject;

	// Data to the GPU
	vbo->Data(mesh.Vertices(), mesh.VertexCount() * sizeof(Vertex), OpenGL::BufferUsage::StaticDraw);

	// Mapping
	vao->BindAttribute(0, *vbo, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), 0U);
	vao->BindAttribute(1, *vbo, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(glm::vec3));
	vao->BindAttribute(2, *vbo, OpenGL::Type::Float, (unsigned)mesh.VertexCount(), sizeof(Vertex), sizeof(glm::vec3) + sizeof(glm::vec2));
}

void Geometry::Draw(void)
{
	if(!m_DataUploaded) {
		throw Exception("Attempted to call Geometry::Draw on empty Geometry");
	}

	OpenGL::Context* context = OpenGL::Context::GetInstance();
	OpenGL::VertexArray* vao = (OpenGL::VertexArray*) m_VertexArrayObject;
	context->DrawArrays(*vao, OpenGL::Primitive::Triangles, 0U, m_DrawCount);
}
