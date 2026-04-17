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

#include <infrastructure/log.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/premade_2d/label.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/util/image.hpp>

rendering_engine::label::label(rendering_engine::util::font* font, float size, const std::string& text)
    : m_font{font}, m_text{text}
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
        const rendering_engine::util::image* image = font->get_image(c, &x0, &y0, &x1, &y1);
        LOG_INF("%c - %i %i %i %i", c, x0, y0, x1, y1);
        const float width = ((float)image->get_width() / image->get_height()) * size;
        auto pane = std::make_unique<rendering_engine::pane>(glm::vec2{width, size});
        pane->set_image(*image);
        pane->transform.set_position(glm::vec3{-0.3f + position, 0.95f, 0.0f});
        position += width + size * 0.1;
        m_panes.push_back(std::move(pane));
    }
}

void rendering_engine::label::upload()
{
    for (auto& p : m_panes)
    {
        p->upload();
    }
}

void rendering_engine::label::render()
{
    for (auto& p : m_panes)
    {
        p->render();
    }
}
