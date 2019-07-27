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

#include <RenderingEngine/Renderables/Overlay.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/Log.hpp>

RenderingEngine::Overlay::Overlay() :
        m_VertexCount { 6 }
{}

void RenderingEngine::Overlay::Upload()
{
    glm::vec2 vertices[6] = {
        glm::vec2{-1.0f, -1.0f},
        glm::vec2{ 1.0f, -1.0f},
        glm::vec2{ 1.0f,  1.0f},

        glm::vec2{-1.0f, -1.0f},
        glm::vec2{ 1.0f,  1.0f},
        glm::vec2{-1.0f,  1.0f},
    };

    m_VertexBufferObject.Data(vertices, sizeof(vertices), RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject.BindAttribute(0, m_VertexBufferObject,
                                      RenderingEngine::OpenGL::Type::Float,
                                      2, sizeof(glm::vec2), 0);
}

void RenderingEngine::Overlay::Render()
{
    Renderer* currentRenderer { RenderingEngine::Renderer::GetCurrentRenderer() };

    if (!currentRenderer)
    {
        LOG_WARN("Attampted to render without renderer attached");
        return;
    }

    RenderingEngine::OpenGL::Context::GetInstance()->DrawArrays(m_VertexArrayObject,
                                                                RenderingEngine::OpenGL::Primitive::Triangles,
                                                                0, m_VertexCount);
}
