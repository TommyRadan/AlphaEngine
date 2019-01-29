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

#include <RenderingEngine/Renderers/OverlayRenderer.hpp>

static std::string vertexShader = R"vs(
        #version 330

        layout(location=0) in vec2 position;

        out vec2 uv;

        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            uv = vec2(position.x / 2 + 0.5, position.y / 2 + 0.5);
        }
)vs";

static std::string fragmentShader = R"fs(
        #version 330

        out vec4 fragColor;
        in vec2 uv;

        uniform sampler2D overlay;

        void main()
        {
            fragColor = texture(overlay, uv);
        }
)fs";

RenderingEngine::Renderers::OverlayRenderer::OverlayRenderer() :
        Renderer {}
{
    ConstructProgram(vertexShader, fragmentShader);
}

RenderingEngine::Renderers::OverlayRenderer* RenderingEngine::Renderers::OverlayRenderer::GetInstance()
{
    static OverlayRenderer* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new OverlayRenderer;
    }

    return instance;
}

RenderingEngine::Renderers::OverlayRenderer::~OverlayRenderer()
{
    DestructProgram();
}
