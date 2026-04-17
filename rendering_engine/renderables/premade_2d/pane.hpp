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

#include <rendering_engine/renderables/renderable.hpp>

#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/util/image.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct pane : public renderable
    {
        pane(const glm::vec2& size);

        void set_color(const rendering_engine::util::color& color);
        void set_image(const rendering_engine::util::image& image);

        rendering_engine::util::transform transform;

        void upload() final;
        void render() final;

    private:
        opengl::vertex_array* m_vertex_array_object;
        opengl::vertex_buffer* m_vertex_buffer;
        opengl::vertex_buffer* m_indicies_buffer;

        unsigned int m_vertex_count;

        glm::vec2 m_size;
        rendering_engine::util::color m_color;
        rendering_engine::opengl::texture* m_texture;
    };
} // namespace rendering_engine
