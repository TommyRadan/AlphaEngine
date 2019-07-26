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

#pragma once

#include <RenderingEngine/OpenGL/Framebuffer.hpp>
#include <RenderingEngine/OpenGL/Program.hpp>
#include <RenderingEngine/OpenGL/Shader.hpp>
#include <RenderingEngine/OpenGL/Texture.hpp>
#include <RenderingEngine/OpenGL/Typedef.hpp>
#include <RenderingEngine/OpenGL/VertexArray.hpp>
#include <RenderingEngine/OpenGL/VertexBuffer.hpp>
#include <Infrastructure/Color.hpp>

namespace RenderingEngine
{
    namespace OpenGL
    {
        struct Context
        {
            static Context* GetInstance();

            void Init();
            void Quit();

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
