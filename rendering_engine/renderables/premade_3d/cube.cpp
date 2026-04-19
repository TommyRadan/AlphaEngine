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
#include <rhi/rhi.hpp>

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

    rhi::device* device = rhi::get_device();

    rhi::buffer_desc vb_desc{};
    vb_desc.initial_data = expanded_vertices;
    vb_desc.size = sizeof(expanded_vertices);
    vb_desc.usage = rhi::buffer_usage::static_draw;
    vb_desc.is_index_buffer = false;
    m_vertex_buffer_object = device->create_buffer(vb_desc);

    m_vertex_array_object = device->create_vertex_array();

    rhi::vertex_attribute_desc attr0{};
    attr0.location = 0;
    attr0.source = m_vertex_buffer_object;
    attr0.type = rhi::element_type::float_type;
    attr0.component_count = 3;
    attr0.stride = sizeof(vertex_postion_normal);
    attr0.offset = 0;
    device->vertex_array_bind_attribute(m_vertex_array_object, attr0);

    rhi::vertex_attribute_desc attr1{};
    attr1.location = 1;
    attr1.source = m_vertex_buffer_object;
    attr1.type = rhi::element_type::float_type;
    attr1.component_count = 3;
    attr1.stride = sizeof(vertex_postion_normal);
    attr1.offset = sizeof(glm::vec3);
    device->vertex_array_bind_attribute(m_vertex_array_object, attr1);
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

    rhi::draw_call call{};
    call.vao = m_vertex_array_object;
    call.topology = rhi::primitive_type::triangles;
    call.indexed = false;
    call.offset = 0;
    call.count = m_vertex_count;
    rhi::get_device()->draw(call);
}
