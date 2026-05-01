/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

#include <cmath>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/premade_3d/sphere.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

rendering_engine::sphere::sphere(unsigned int stacks, unsigned int slices)
    : m_stacks{stacks}, m_slices{slices}, m_index_count{0}, m_vertex_array_object{nullptr},
      m_vertex_buffer_object{nullptr}, m_index_buffer_object{nullptr}
{
}

void rendering_engine::sphere::upload()
{
    const unsigned int rings = m_stacks + 1;
    const unsigned int columns = m_slices + 1;

    std::vector<vertex_position_uv_normal> vertices;
    vertices.reserve(rings * columns);

    constexpr float pi = 3.14159265358979323846f;

    for (unsigned int i = 0; i < rings; ++i)
    {
        const float v = static_cast<float>(i) / static_cast<float>(m_stacks);
        const float phi = pi * v;
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (unsigned int j = 0; j < columns; ++j)
        {
            const float u = static_cast<float>(j) / static_cast<float>(m_slices);
            const float theta = 2.0f * pi * u;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            vertex_position_uv_normal vertex;
            vertex.pos = infrastructure::math::vec3{sin_phi * cos_theta, sin_phi * sin_theta, cos_phi};
            vertex.normal = vertex.pos;
            vertex.uv = infrastructure::math::vec2{u, 1.0f - v};
            vertices.push_back(vertex);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(m_stacks * m_slices * 6);

    for (unsigned int i = 0; i < m_stacks; ++i)
    {
        for (unsigned int j = 0; j < m_slices; ++j)
        {
            const uint32_t a = i * columns + j;
            const uint32_t b = a + 1;
            const uint32_t c = a + columns;
            const uint32_t d = c + 1;

            // CCW winding when viewed from outside the sphere — going
            // top-left -> bottom-left -> bottom-right traces a CCW loop in
            // screen space when the outward normal points toward the camera.
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);

            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
        }
    }

    m_index_count = static_cast<unsigned int>(indices.size());

    auto& gl = *control::current_engine().opengl;

    // Order matters: create + bind the VAO before uploading the index buffer.
    // vertex_buffer::element_data binds GL_ELEMENT_ARRAY_BUFFER, which is
    // tracked as VAO state — if no VAO is bound (or the wrong one is bound),
    // the binding either gets lost or, worse, corrupts a previously created
    // sphere's VAO. Creating + binding our own VAO first guarantees the
    // element buffer ends up associated with this sphere's VAO and nothing
    // else is touched.
    m_vertex_buffer_object = gl.create_vbo();
    m_vertex_buffer_object->data(vertices.data(),
                                 vertices.size() * sizeof(vertex_position_uv_normal),
                                 rendering_engine::opengl::buffer_usage::static_draw);

    m_vertex_array_object = gl.create_vao();
    m_vertex_array_object->bind_attribute(
        0, *m_vertex_buffer_object, rendering_engine::opengl::type::Float, 3, sizeof(vertex_position_uv_normal), 0);
    m_vertex_array_object->bind_attribute(1,
                                          *m_vertex_buffer_object,
                                          rendering_engine::opengl::type::Float,
                                          2,
                                          sizeof(vertex_position_uv_normal),
                                          sizeof(infrastructure::math::vec3));
    m_vertex_array_object->bind_attribute(2,
                                          *m_vertex_buffer_object,
                                          rendering_engine::opengl::type::Float,
                                          3,
                                          sizeof(vertex_position_uv_normal),
                                          sizeof(infrastructure::math::vec3) + sizeof(infrastructure::math::vec2));

    // VAO is bound now — element_data's glBindBuffer(GL_ELEMENT_ARRAY_BUFFER)
    // will be captured into this VAO's state. bind_elements then re-asserts
    // it (cheap, and makes intent explicit).
    m_index_buffer_object = gl.create_vbo();
    m_index_buffer_object->element_data(
        indices.data(), indices.size() * sizeof(uint32_t), rendering_engine::opengl::buffer_usage::static_draw);
    m_vertex_array_object->bind_elements(*m_index_buffer_object);

    // Some GL drivers don't commit a freshly configured VAO's state to its
    // saved state until the VAO is unbound at least once — without this,
    // the very first draw of this VAO fails silently (no fragments) until
    // something else binds and unbinds another VAO first.
    gl.unbind_vao();
}

void rendering_engine::sphere::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render sphere without renderer attached");
        return;
    }

    current_renderer->upload_matrix4("modelMatrix", this->transform.get_transform_matrix());
    current_renderer->setup_options(options);

    control::current_engine().opengl->draw_elements(*m_vertex_array_object,
                                                    rendering_engine::opengl::primitive::triangles,
                                                    0,
                                                    m_index_count,
                                                    rendering_engine::opengl::type::unsigned_int);
}

const rendering_engine::opengl::vertex_array& rendering_engine::sphere::get_vao() const
{
    return *m_vertex_array_object;
}

unsigned int rendering_engine::sphere::get_index_count() const
{
    return m_index_count;
}
