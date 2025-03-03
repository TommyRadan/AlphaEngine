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

#include <RenderingEngine/Renderers/BasicRenderer.hpp>

static std::string vertexShader = R"vs(
        #version 330

        layout(location=0) in vec3 position;

        uniform mat4 modelMatrix;
        uniform mat4 viewMatrix;
        uniform mat4 projectionMatrix;

        void main()
        {
            mat4 MVP = projectionMatrix * viewMatrix * modelMatrix;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

static std::string fragmentShader = R"fs(
        #version 330

        out vec4 fragColor;

        uniform vec4 color;

        void main()
        {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
)fs";

RenderingEngine::Renderers::BasicRenderer::BasicRenderer() :
    Renderer {}
{
    ConstructProgram(vertexShader, fragmentShader);
}

RenderingEngine::Renderers::BasicRenderer::~BasicRenderer()
{
    DestructProgram();
}
