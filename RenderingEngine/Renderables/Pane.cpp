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

#include <RenderingEngine/Renderables/Pane.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Mesh/Vertex.hpp>
#include <Infrastructure/Log.hpp>

RenderingEngine::Pane::Pane(const glm::vec2& size, const Infrastructure::Color& color) :
    m_VertexCount{ 0 },
    m_VertexArrayObject { nullptr },
    m_VertexBuffer { nullptr },
    m_IndiciesBuffer{ nullptr },
    m_Size { size },
    m_Color { color }
{}

void RenderingEngine::Pane::Upload()
{
    this->m_VertexCount = 6;

    glm::vec3 position { this->transform.GetPosition() };

    VertexPosition vertex[4];
    vertex[0].Pos = glm::vec3 { position.x, position.y - m_Size.y, -1.0f };
    vertex[1].Pos = glm::vec3 { position.x + m_Size.x, position.y - m_Size.y, -1.0f };
    vertex[2].Pos = glm::vec3 { position.x + m_Size.x, position.y, -1.0f };
    vertex[3].Pos = glm::vec3 { position.x, position.y, -1.0f };

    uint32_t indicies[6] = { 3, 0, 1, 3, 1, 2 };

    m_VertexBuffer = OpenGL::Context::GetInstance()->CreateVBO();
    m_VertexBuffer->Data(vertex, sizeof(vertex), RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_IndiciesBuffer = OpenGL::Context::GetInstance()->CreateVBO();
    m_IndiciesBuffer->ElementData(indicies, sizeof(indicies), RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject = OpenGL::Context::GetInstance()->CreateVAO();
    m_VertexArrayObject->BindAttribute(0, *m_VertexBuffer, RenderingEngine::OpenGL::Type::Float, 3, sizeof(VertexPosition), 0);

    m_VertexArrayObject->BindElements(*m_IndiciesBuffer);
}

void RenderingEngine::Pane::Render()
{
    Renderer* currentRenderer{ RenderingEngine::Renderer::GetCurrentRenderer() };

    if (!currentRenderer)
    {
        LOG_WARN("Attampted to render without renderer attached");
        return;
    }

    options.Colors["color"] = this->m_Color;
    currentRenderer->SetupOptions(options);

    RenderingEngine::OpenGL::Context::GetInstance()->DrawElements(
        *m_VertexArrayObject, RenderingEngine::OpenGL::Primitive::Triangles,
        0, m_VertexCount, RenderingEngine::OpenGL::Type::UnsignedInt);
}
