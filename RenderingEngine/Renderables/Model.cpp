#include <RenderingEngine/Renderables/Model.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>

RenderingEngine::Model::Model() :
    m_VertexCount { 0 }
{}

void RenderingEngine::Model::UploadMesh(const RenderingEngine::Mesh& mesh)
{
    m_VertexCount = mesh.VertexCount();

    m_VertexBufferObject.Data(mesh.Vertices(),
                              m_VertexCount * sizeof(RenderingEngine::Vertex),
                              RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject.BindAttribute(0, m_VertexBufferObject,
                                      RenderingEngine::OpenGL::Type::Float,
                                      3, sizeof(RenderingEngine::Vertex), 0);

    m_VertexArrayObject.BindAttribute(1, m_VertexBufferObject,
                                      RenderingEngine::OpenGL::Type::Float,
                                      2, sizeof(RenderingEngine::Vertex), sizeof(glm::vec3));

    m_VertexArrayObject.BindAttribute(2, m_VertexBufferObject,
                                      RenderingEngine::OpenGL::Type::Float,
                                      3, sizeof(RenderingEngine::Vertex), sizeof(glm::vec3) + sizeof(glm::vec2));
}

void RenderingEngine::Model::Render()
{
    RenderingEngine::Renderer::GetCurrentRenderer()->UploadMatrix4("modelMatrix", this->GetModelMatrix());
	RenderingEngine::Renderer::GetCurrentRenderer()->SetupOptions(options);

    RenderingEngine::OpenGL::Context::GetInstance()->DrawArrays(m_VertexArrayObject,
                                                                RenderingEngine::OpenGL::Primitive::Triangles,
                                                                0, m_VertexCount);
}