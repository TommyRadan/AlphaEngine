/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <RenderingEngine/Renderables/Premade3D/Cube.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Mesh/Vertex.hpp>
#include <Infrastructure/log.hpp>

RenderingEngine::Cube::Cube() :
    m_VertexCount { 0 },
    m_VertexArrayObject { nullptr },
    m_VertexBufferObject { nullptr }
{}

void RenderingEngine::Cube::Upload()
{
    this->m_VertexCount = 36;

    VertexPostionNormal vertices[8];

    vertices[0].Pos = glm::vec3 {-1.0f, -1.0f,  1.0f};
    vertices[1].Pos = glm::vec3 { 1.0f, -1.0f,  1.0f};
    vertices[2].Pos = glm::vec3 { 1.0f,  1.0f,  1.0f};
    vertices[3].Pos = glm::vec3 {-1.0f,  1.0f,  1.0f};
    vertices[4].Pos = glm::vec3 {-1.0f, -1.0f, -1.0f};
    vertices[5].Pos = glm::vec3 { 1.0f, -1.0f, -1.0f};
    vertices[6].Pos = glm::vec3 { 1.0f,  1.0f, -1.0f};
    vertices[7].Pos = glm::vec3 {-1.0f,  1.0f, -1.0f};

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

    VertexPostionNormal expandedVertices[36];

    for (int i = 0; i < 36; i++)
    {
        expandedVertices[i].Pos = vertices[indicies[i]].Pos;

        int sector = i/6;

        switch (sector)
        {
            case 0:
                expandedVertices[i].Normal = glm::vec3{ 0.0f, 0.0f, 1.0f};
                break;
            case 1:
                expandedVertices[i].Normal = glm::vec3{ 1.0f, 0.0f, 0.0f};
                break;
            case 2:
                expandedVertices[i].Normal = glm::vec3{ 0.0f, 0.0f,-1.0f};
                break;
            case 3:
                expandedVertices[i].Normal = glm::vec3{-1.0f, 0.0f, 0.0f};
                break;
            case 4:
                expandedVertices[i].Normal = glm::vec3{ 0.0f,-1.0f, 0.0f};
                break;
            case 5:
                expandedVertices[i].Normal = glm::vec3{ 0.0f, 1.0f, 0.0f};
                break;
            default:
                expandedVertices[i].Normal = glm::vec3{ 0.0f, 0.0f, 0.0f};
        }
    }

    m_VertexBufferObject = OpenGL::Context::get_instance().CreateVBO();
    m_VertexBufferObject->Data(expandedVertices, sizeof(expandedVertices), RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject = OpenGL::Context::get_instance().CreateVAO();
    m_VertexArrayObject->BindAttribute(0, *m_VertexBufferObject,
                                       RenderingEngine::OpenGL::Type::Float,
                                       3, sizeof(VertexPostionNormal), 0);

    m_VertexArrayObject->BindAttribute(1, *m_VertexBufferObject,
                                       RenderingEngine::OpenGL::Type::Float,
                                       3, sizeof(VertexPostionNormal), sizeof(glm::vec3));
}

void RenderingEngine::Cube::Render()
{
    Renderer* currentRenderer { RenderingEngine::Renderer::GetCurrentRenderer() };

    if (!currentRenderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    currentRenderer->UploadMatrix4("modelMatrix", this->transform.GetTransformMatrix());
    currentRenderer->SetupOptions(options);

    RenderingEngine::OpenGL::Context::get_instance().DrawArrays(*m_VertexArrayObject,
                                                                  RenderingEngine::OpenGL::Primitive::Triangles,
                                                                  0, m_VertexCount);
}
