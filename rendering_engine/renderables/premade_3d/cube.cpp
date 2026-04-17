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
#include <rendering_engine/renderables/premade_3d/cube.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::cube::cube() : m_vertex_count{0}, m_vertex_array_object{nullptr}, m_vertex_buffer_object{nullptr} {}

void rendering_engine::cube::upload()
{
    this->m_vertex_count = 36;

    vertex_postion_normal vertices[8];

    vertices[0].pos = glm::vec3{-1.0f, -1.0f, 1.0f};
    vertices[1].pos = glm::vec3{1.0f, -1.0f, 1.0f};
    vertices[2].pos = glm::vec3{1.0f, 1.0f, 1.0f};
    vertices[3].pos = glm::vec3{-1.0f, 1.0f, 1.0f};
    vertices[4].pos = glm::vec3{-1.0f, -1.0f, -1.0f};
    vertices[5].pos = glm::vec3{1.0f, -1.0f, -1.0f};
    vertices[6].pos = glm::vec3{1.0f, 1.0f, -1.0f};
    vertices[7].pos = glm::vec3{-1.0f, 1.0f, -1.0f};

    uint32_t indicies[36] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                             4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};

    vertex_postion_normal expanded_vertices[36];

    for (int i = 0; i < 36; i++)
    {
        expanded_vertices[i].pos = vertices[indicies[i]].pos;

        int sector = i / 6;

        switch (sector)
        {
        case 0:
            expanded_vertices[i].normal = glm::vec3{0.0f, 0.0f, 1.0f};
            break;
        case 1:
            expanded_vertices[i].normal = glm::vec3{1.0f, 0.0f, 0.0f};
            break;
        case 2:
            expanded_vertices[i].normal = glm::vec3{0.0f, 0.0f, -1.0f};
            break;
        case 3:
            expanded_vertices[i].normal = glm::vec3{-1.0f, 0.0f, 0.0f};
            break;
        case 4:
            expanded_vertices[i].normal = glm::vec3{0.0f, -1.0f, 0.0f};
            break;
        case 5:
            expanded_vertices[i].normal = glm::vec3{0.0f, 1.0f, 0.0f};
            break;
        default:
            expanded_vertices[i].normal = glm::vec3{0.0f, 0.0f, 0.0f};
        }
    }

    m_vertex_buffer_object = opengl::context::get_instance().create_vbo();
    m_vertex_buffer_object->data(
        expanded_vertices, sizeof(expanded_vertices), rendering_engine::opengl::buffer_usage::static_draw);

    m_vertex_array_object = opengl::context::get_instance().create_vao();
    m_vertex_array_object->bind_attribute(
        0, *m_vertex_buffer_object, rendering_engine::opengl::type::Float, 3, sizeof(vertex_postion_normal), 0);

    m_vertex_array_object->bind_attribute(1,
                                       *m_vertex_buffer_object,
                                       rendering_engine::opengl::type::Float,
                                       3,
                                       sizeof(vertex_postion_normal),
                                       sizeof(glm::vec3));
}

void rendering_engine::cube::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    current_renderer->upload_matrix4("modelMatrix", this->transform.get_transform_matrix());
    current_renderer->setup_options(options);

    rendering_engine::opengl::context::get_instance().draw_arrays(
        *m_vertex_array_object, rendering_engine::opengl::primitive::triangles, 0, m_vertex_count);
}
