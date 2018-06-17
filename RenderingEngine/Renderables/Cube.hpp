#pragma once

#include <RenderingEngine/Renderables/Renderable.hpp>

#include <RenderingEngine/OpenGL/OpenGL.hpp>
#include <Infrastructure/Transform.hpp>

namespace RenderingEngine
{
    struct Cube : public Renderable
    {
        Cube();

        Infrastructure::Transform transform;

        void Upload();
        void Render() final;

    private:
        OpenGL::VertexArray m_VertexArrayObject;
        OpenGL::VertexBuffer m_Verticies;
        OpenGL::VertexBuffer m_Indicies;

        unsigned int m_VertexCount;
    };
}
