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

#include <memory>

#include <infrastructure/log.hpp>
#include <rendering_engine/material/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/premade_2d/pane.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::pane::pane(const glm::vec2& size)
    : m_vertex_count{0}, m_vertex_array_object{nullptr}, m_vertex_buffer{nullptr}, m_indicies_buffer{nullptr},
      m_size{size}, m_color{0, 0, 0, 0}, m_texture{nullptr}
{
    shared_material = std::make_shared<rendering_engine::material>();
}

void rendering_engine::pane::set_color(const rendering_engine::util::color& color)
{
    m_color = color;
}

void rendering_engine::pane::set_image(const rendering_engine::util::image& image)
{
    m_texture = rendering_engine::opengl::context::get_instance().create_texture();
    m_texture->image2_d(image.get_pixels(),
                        rendering_engine::opengl::data_type::unsigned_byte,
                        rendering_engine::opengl::format::rgba,
                        image.get_width(),
                        image.get_height(),
                        rendering_engine::opengl::internal_format::rgba);

    m_texture->set_wrapping_r(opengl::wrapping::clamp_edge);
    m_texture->set_wrapping_s(opengl::wrapping::clamp_edge);
    m_texture->set_wrapping_t(opengl::wrapping::clamp_edge);

    m_texture->set_filters(opengl::filter::nearest, opengl::filter::nearest);
    m_texture->generate_mipmaps();
}

void rendering_engine::pane::upload()
{
    this->m_vertex_count = 6;

    glm::vec3 position{this->transform.get_position()};

    vertex_position_uv vertex[4];
    vertex[0].pos = glm::vec3{position.x, position.y - m_size.y, -1.0f};
    vertex[0].uv = glm::vec2{0.0f, 0.0f};
    vertex[1].pos = glm::vec3{position.x + m_size.x, position.y - m_size.y, -1.0f};
    vertex[1].uv = glm::vec2{1.0f, 0.0f};
    vertex[2].pos = glm::vec3{position.x + m_size.x, position.y, -1.0f};
    vertex[2].uv = glm::vec2{1.0f, 1.0f};
    vertex[3].pos = glm::vec3{position.x, position.y, -1.0f};
    vertex[3].uv = glm::vec2{0.0f, 1.0f};

    uint32_t indicies[6] = {3, 0, 1, 3, 1, 2};

    m_vertex_buffer = opengl::context::get_instance().create_vbo();
    m_vertex_buffer->data(vertex, sizeof(vertex), rendering_engine::opengl::buffer_usage::static_draw);

    m_indicies_buffer = opengl::context::get_instance().create_vbo();
    m_indicies_buffer->element_data(indicies, sizeof(indicies), rendering_engine::opengl::buffer_usage::static_draw);

    m_vertex_array_object = opengl::context::get_instance().create_vao();
    m_vertex_array_object->bind_attribute(
        0, *m_vertex_buffer, rendering_engine::opengl::type::Float, 3, sizeof(vertex_position_uv), 0);
    m_vertex_array_object->bind_attribute(
        1, *m_vertex_buffer, rendering_engine::opengl::type::Float, 2, sizeof(vertex_position_uv), sizeof(glm::vec3));
    m_vertex_array_object->bind_elements(*m_indicies_buffer);
}

void rendering_engine::pane::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    shared_material->set_uniform("color", this->m_color);
    if (this->m_texture)
    {
        shared_material->set_texture("tex", this->m_texture);
        shared_material->set_uniform("useTexture", 1.0f);
    }
    else
    {
        shared_material->set_uniform("useTexture", 0.0f);
    }
    shared_material->bind();

    rendering_engine::opengl::context::get_instance().draw_elements(*m_vertex_array_object,
                                                                    rendering_engine::opengl::primitive::triangles,
                                                                    0,
                                                                    m_vertex_count,
                                                                    rendering_engine::opengl::type::unsigned_int);
}
