#include <RenderingEngine/Renderables/Cube.hpp>
#include <RenderingEngine/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>

RenderingEngine::Cube::Cube() :
        m_VertexCount { 8 }
{}

void RenderingEngine::Cube::Upload()
{
    glm::vec3 vertices[8];

    vertices[0] = glm::vec3 {-1.0f, -1.0f,  1.0f};
    vertices[1] = glm::vec3 { 1.0f, -1.0f,  1.0f};
    vertices[2] = glm::vec3 { 1.0f,  1.0f,  1.0f};
    vertices[3] = glm::vec3 {-1.0f,  1.0f,  1.0f};
    vertices[4] = glm::vec3 {-1.0f, -1.0f, -1.0f};
    vertices[5] = glm::vec3 { 1.0f, -1.0f, -1.0f};
    vertices[6] = glm::vec3 { 1.0f,  1.0f, -1.0f};
    vertices[7] = glm::vec3 {-1.0f,  1.0f, -1.0f};

    uint32_t indicies[36] = {
            0, 1, 2,
            2, 3, 0,
            1, 5, 6,
            6, 2, 1,
            7, 6, 5,
            5, 4, 7,
            4, 0, 3,
            3, 7, 4,
            4, 5, 1,
            1, 0, 4,
            3, 2, 6,
            6, 7, 3
    };

    m_Verticies.Data(vertices, m_VertexCount * sizeof(glm::vec3), RenderingEngine::OpenGL::BufferUsage::StaticDraw);
    m_Indicies.Data(indicies, 36 * sizeof(uint32_t), RenderingEngine::OpenGL::BufferUsage::StaticDraw);
    m_VertexArrayObject.BindAttribute(0, m_Verticies, RenderingEngine::OpenGL::Type::Float, 3, sizeof(glm::vec3), 0);
    m_VertexArrayObject.BindElements(m_Indicies);
}

void RenderingEngine::Cube::Render()
{
    Renderer* currentRenderer { RenderingEngine::Context::GetInstance()->GetCurrentRenderer() };

    if (!currentRenderer)
    {
        return;
    }

    currentRenderer->UploadMatrix4("modelMatrix", this->transform.GetTransformMatrix());
    currentRenderer->SetupOptions(options);

    RenderingEngine::OpenGL::Context::GetInstance()->DrawElements(m_VertexArrayObject,
                                                                  RenderingEngine::OpenGL::Primitive::Triangles,
                                                                  0, m_VertexCount,
                                                                  RenderingEngine::OpenGL::Type::UnsignedInt);
}
