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

#include <RenderingEngine/Renderables/Premade2D/Label.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Mesh/Vertex.hpp>
#include <Infrastructure/log.hpp>
#include <RenderingEngine/Util/Image.hpp>

RenderingEngine::Label::Label(RenderingEngine::Util::Font* font, float size, const std::string& text) :
    m_Font{ font },
    m_Text{ text }
{
    float position = 0.0f;

    for (auto& c : text)
    {
        if (c == ' ')
        {
            position += size * 0.3;
            continue;
        }

        int x0, y0, x1, y1;
        const RenderingEngine::Util::Image* image = font->GetImage(c, &x0, &y0, &x1, &y1);
        LOG_INF("%c - %i %i %i %i", c, x0, y0, x1, y1);
        const float width = ((float)image->GetWidth() / image->GetHeight()) * size;
        RenderingEngine::Pane *pane = new RenderingEngine::Pane(glm::vec2{ width, size });
        pane->SetImage(*image);
        pane->transform.SetPosition(glm::vec3{ -0.3f + position, 0.95f, 0.0f });
        position += width + size * 0.1;
        m_Panes.push_back(pane);
    }
}

void RenderingEngine::Label::Upload()
{
    for (auto& p : m_Panes)
    {
        p->Upload();
    }
}

void RenderingEngine::Label::Render()
{
    for (auto& p : m_Panes)
    {
        p->Render();
    }
}
