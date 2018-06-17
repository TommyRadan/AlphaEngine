#pragma once

#include <RenderingEngine/OpenGL/Framebuffer.hpp>
#include <RenderingEngine/OpenGL/Program.hpp>
#include <RenderingEngine/OpenGL/Shader.hpp>
#include <RenderingEngine/OpenGL/Texture.hpp>
#include <RenderingEngine/OpenGL/Typedef.hpp>
#include <RenderingEngine/OpenGL/VertexArray.hpp>
#include <RenderingEngine/OpenGL/VertexBuffer.hpp>
#include <Infrastructure/Color.hpp>

#include <Infrastructure/Subsystem.hpp>

namespace RenderingEngine
{
    namespace OpenGL
    {
        struct Context : public Infrastructure::Subsystem
        {
            static Context* GetInstance();

            void Init() final;
            void Quit() final;

            void Enable(OpenGL::Capability capability);
            void Disable(OpenGL::Capability capability);

            void ClearColor(const Infrastructure::Color& color);
            void Clear(OpenGL::Buffer buffers);
            void DepthMask(bool writeEnabled);

            void BindTexture(const OpenGL::Texture& texture, uint8_t unit);
            void BindFramebuffer(const OpenGL::Framebuffer& framebuffer);
            void BindFramebuffer();

            void DrawArrays(const OpenGL::VertexArray& vao,
                            OpenGL::Primitive mode,
                            unsigned int offset,
                            size_t vertices);

            void DrawElements(const OpenGL::VertexArray& vao,
                              OpenGL::Primitive mode,
                              intptr_t offset,
                              unsigned int count,
                              RenderingEngine::OpenGL::Type type);
        };
    }
}
