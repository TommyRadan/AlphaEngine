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

        layout(location=0) in vec3 position;
        layout(location=1) in vec2 uv;

        out vec2 texCoord;

        void main()
        {
            texCoord = uv;
            gl_Position = vec4(position, 1.0);
        }
)vs";

static std::string fragmentShader = R"fs(
        #version 330

        out vec4 fragColor;
        in vec2 texCoord;

        uniform float useTexture = 0.0;
        uniform vec4 color;
        uniform sampler2D tex;

        void main()
        {
            fragColor = (useTexture != 0.0) ? texture(tex, vec2(texCoord.x, 1.0 - texCoord.y)) : color;
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
