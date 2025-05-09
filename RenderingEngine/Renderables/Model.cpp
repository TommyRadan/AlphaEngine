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

#include <RenderingEngine/Renderables/Model.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <Infrastructure/log.hpp>

RenderingEngine::Model::Model() :
    m_VertexCount { 0 },
    m_VertexArrayObject { nullptr },
    m_VertexBufferObject { nullptr }
{}

void RenderingEngine::Model::UploadMesh(const RenderingEngine::Mesh& mesh)
{
    m_VertexCount = mesh.VertexCount();

    m_VertexBufferObject = OpenGL::Context::get_instance().CreateVBO();

    m_VertexBufferObject->Data(mesh.Vertices(),
                               m_VertexCount * sizeof(RenderingEngine::VertexPositionUvNormal),
                               RenderingEngine::OpenGL::BufferUsage::StaticDraw);

    m_VertexArrayObject = OpenGL::Context::get_instance().CreateVAO();

    m_VertexArrayObject->BindAttribute(0, *m_VertexBufferObject,
                                       RenderingEngine::OpenGL::Type::Float,
                                       3, sizeof(RenderingEngine::VertexPositionUvNormal), 0);

    m_VertexArrayObject->BindAttribute(1, *m_VertexBufferObject,
                                       RenderingEngine::OpenGL::Type::Float,
                                       2, sizeof(RenderingEngine::VertexPositionUvNormal), sizeof(glm::vec3));

    m_VertexArrayObject->BindAttribute(2, *m_VertexBufferObject,
                                       RenderingEngine::OpenGL::Type::Float,
                                       3, sizeof(RenderingEngine::VertexPositionUvNormal), sizeof(glm::vec3) + sizeof(glm::vec2));
}

void RenderingEngine::Model::Render()
{
    Renderer* currentRenderer { RenderingEngine::Renderer::GetCurrentRenderer() };

    if (!currentRenderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    currentRenderer->UploadMatrix4("modelMatrix", transform.GetTransformMatrix());
	currentRenderer->SetupOptions(options);

    RenderingEngine::OpenGL::Context::get_instance().DrawArrays(*m_VertexArrayObject,
                                                                RenderingEngine::OpenGL::Primitive::Triangles,
                                                                0, m_VertexCount);
}
