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

#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderables/premade_3d/cubed_sphere.hpp>
#include <rendering_engine/renderers/renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>

namespace
{
    // Per-face anchors. For each cube face, `origin` is the (u=0, v=0)
    // corner of the face on the unit cube and (`u_axis`, `v_axis`) are the
    // world-space vectors that traverse the face from u=0->1 and v=0->1
    // respectively. Conventions match standard GL cube maps: u increases to
    // the right when looking at the face from outside, v increases downward.
    struct face_def
    {
        infrastructure::math::vec3 origin;
        infrastructure::math::vec3 u_axis;
        infrastructure::math::vec3 v_axis;
    };

    const face_def faces[6] = {
        // +X (right)
        {infrastructure::math::vec3{+1.0f, +1.0f, +1.0f},
         infrastructure::math::vec3{0.0f, 0.0f, -2.0f},
         infrastructure::math::vec3{0.0f, -2.0f, 0.0f}},
        // -X (left)
        {infrastructure::math::vec3{-1.0f, +1.0f, -1.0f},
         infrastructure::math::vec3{0.0f, 0.0f, +2.0f},
         infrastructure::math::vec3{0.0f, -2.0f, 0.0f}},
        // +Y (top)
        {infrastructure::math::vec3{-1.0f, +1.0f, -1.0f},
         infrastructure::math::vec3{+2.0f, 0.0f, 0.0f},
         infrastructure::math::vec3{0.0f, 0.0f, +2.0f}},
        // -Y (bottom)
        {infrastructure::math::vec3{-1.0f, -1.0f, +1.0f},
         infrastructure::math::vec3{+2.0f, 0.0f, 0.0f},
         infrastructure::math::vec3{0.0f, 0.0f, -2.0f}},
        // +Z (back)
        {infrastructure::math::vec3{-1.0f, +1.0f, +1.0f},
         infrastructure::math::vec3{+2.0f, 0.0f, 0.0f},
         infrastructure::math::vec3{0.0f, -2.0f, 0.0f}},
        // -Z (front)
        {infrastructure::math::vec3{+1.0f, +1.0f, -1.0f},
         infrastructure::math::vec3{-2.0f, 0.0f, 0.0f},
         infrastructure::math::vec3{0.0f, -2.0f, 0.0f}},
    };
} // namespace

rendering_engine::cubed_sphere::cubed_sphere(unsigned int subdivisions)
    : m_subdivisions{subdivisions}, m_index_count{0}, m_vertex_array_object{nullptr}, m_vertex_buffer_object{nullptr},
      m_index_buffer_object{nullptr}
{
}

void rendering_engine::cubed_sphere::upload()
{
    const unsigned int n = m_subdivisions;
    const unsigned int rows = n + 1;
    const unsigned int verts_per_face = rows * rows;
    const unsigned int quads_per_face = n * n;

    std::vector<vertex_position_uv_normal> vertices;
    vertices.reserve(6 * verts_per_face);

    std::vector<uint32_t> indices;
    indices.reserve(6 * quads_per_face * 6);

    for (int face = 0; face < 6; ++face)
    {
        const auto& f = faces[face];
        const uint32_t base = static_cast<uint32_t>(vertices.size());

        for (unsigned int j = 0; j < rows; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(n);
            for (unsigned int i = 0; i < rows; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(n);
                const infrastructure::math::vec3 cube_pos = f.origin + u * f.u_axis + v * f.v_axis;
                const infrastructure::math::vec3 sphere_pos = infrastructure::math::normalize(cube_pos);

                vertex_position_uv_normal vertex;
                vertex.pos = sphere_pos;
                vertex.normal = sphere_pos;
                vertex.uv = infrastructure::math::vec2{u, v};
                vertices.push_back(vertex);
            }
        }

        for (unsigned int j = 0; j < n; ++j)
        {
            for (unsigned int i = 0; i < n; ++i)
            {
                const uint32_t a = base + j * rows + i; // (i,   j)   — top-left
                const uint32_t b = a + 1;               // (i+1, j)   — top-right
                const uint32_t c = a + rows;            // (i,   j+1) — bottom-left
                const uint32_t d = c + 1;               // (i+1, j+1) — bottom-right

                // CCW winding when viewed from outside: TL -> BL -> BR
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(d);

                indices.push_back(a);
                indices.push_back(d);
                indices.push_back(b);
            }
        }
    }

    m_index_count = static_cast<unsigned int>(indices.size());

    auto& gl = *control::current_engine().opengl;

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

    m_index_buffer_object = gl.create_vbo();
    m_index_buffer_object->element_data(
        indices.data(), indices.size() * sizeof(uint32_t), rendering_engine::opengl::buffer_usage::static_draw);
    m_vertex_array_object->bind_elements(*m_index_buffer_object);

    // Same VAO-commit workaround as `sphere::upload` — without unbinding here
    // the first draw of this VAO silently produces no fragments on some
    // drivers.
    gl.unbind_vao();
}

void rendering_engine::cubed_sphere::render()
{
    renderer* current_renderer{rendering_engine::renderer::get_current_renderer()};

    if (!current_renderer)
    {
        LOG_WRN("Attempted to render cubed_sphere without renderer attached");
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

const rendering_engine::opengl::vertex_array& rendering_engine::cubed_sphere::get_vao() const
{
    return *m_vertex_array_object;
}

unsigned int rendering_engine::cubed_sphere::get_index_count() const
{
    return m_index_count;
}
