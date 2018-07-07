/**
 * Copyright (c) 2018 Tomislav Radanovic
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

#include <RenderingEngine/Renderables/Cube.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/Log.hpp>

RenderingEngine::Cube::Cube() :
        m_VertexCount { 8 },
        m_IndiciesCount { 36 }
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

    m_Verticies.Data(vertices, sizeof(vertices), RenderingEngine::OpenGL::BufferUsage::StaticDraw);
    m_Indicies.ElementData(indicies, sizeof(indicies), RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject.BindAttribute(0, m_Verticies, RenderingEngine::OpenGL::Type::Float, 3, sizeof(glm::vec3), 0);
    m_VertexArrayObject.BindElements(m_Indicies);
}

void RenderingEngine::Cube::Render()
{
    Renderer* currentRenderer { RenderingEngine::Context::GetInstance()->GetCurrentRenderer() };

    if (!currentRenderer)
    {
        LOG_WARN("Attampted to render without renderer attached");
        return;
    }

    currentRenderer->UploadMatrix4("modelMatrix", this->transform.GetTransformMatrix());
    currentRenderer->SetupOptions(options);

    RenderingEngine::OpenGL::Context::GetInstance()->DrawElements(m_VertexArrayObject,
                                                                  RenderingEngine::OpenGL::Primitive::Triangles,
                                                                  0, m_IndiciesCount,
                                                                  RenderingEngine::OpenGL::Type::UnsignedInt);
}
