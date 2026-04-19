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

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/renderables/model.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::model::model() : m_vertex_count{0}, m_vertex_array_object{nullptr}, m_vertex_buffer_object{nullptr} {}

void rendering_engine::model::upload_mesh(const rendering_engine::mesh& mesh)
{
    m_vertex_count = mesh.vertex_count();

    m_vertex_buffer_object = control::current_engine().opengl->create_vbo();

    m_vertex_buffer_object->data(mesh.vertices(),
                                 m_vertex_count * sizeof(rendering_engine::vertex_position_uv_normal),
                                 rendering_engine::opengl::buffer_usage::static_draw);

    m_vertex_array_object = control::current_engine().opengl->create_vao();

    m_vertex_array_object->bind_attribute(0,
                                          *m_vertex_buffer_object,
                                          rendering_engine::opengl::type::Float,
                                          3,
                                          sizeof(rendering_engine::vertex_position_uv_normal),
                                          0);

    m_vertex_array_object->bind_attribute(1,
                                          *m_vertex_buffer_object,
                                          rendering_engine::opengl::type::Float,
                                          2,
                                          sizeof(rendering_engine::vertex_position_uv_normal),
                                          sizeof(glm::vec3));

    m_vertex_array_object->bind_attribute(2,
                                          *m_vertex_buffer_object,
                                          rendering_engine::opengl::type::Float,
                                          3,
                                          sizeof(rendering_engine::vertex_position_uv_normal),
                                          sizeof(glm::vec3) + sizeof(glm::vec2));
}

void rendering_engine::model::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render without renderer attached");
        return;
    }

    current_renderer->upload_matrix4("modelMatrix", transform.get_transform_matrix());
    current_renderer->setup_options(options);

    control::current_engine().opengl->draw_arrays(
        *m_vertex_array_object, rendering_engine::opengl::primitive::triangles, 0, m_vertex_count);
}
