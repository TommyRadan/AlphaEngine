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

#include <rendering_engine/renderables/premade_3d/cubed_sphere.hpp>

#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderers/renderer.hpp>

namespace
{
    // Per-face anchors. For each cube face, `origin` is the
    // (u=0, v=0) corner of the face on the unit cube and
    // (`u_axis`, `v_axis`) are the world-space vectors that traverse
    // the face from u=0->1 and v=0->1 respectively. Conventions match
    // standard GL cube maps: u increases to the right when looking at
    // the face from outside, v increases downward.
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

rendering_engine::cubed_sphere::cubed_sphere(unsigned int subdivisions) : m_subdivisions{subdivisions} {}

rendering_engine::cubed_sphere::~cubed_sphere()
{
    auto& gpu = *control::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
    if (m_index_buffer.valid())
    {
        gpu.destroy(m_index_buffer);
        m_index_buffer = {};
    }
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
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
    m_vertex_stride = sizeof(vertex_position_uv_normal);

    auto& gpu = *control::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = vertices.size() * sizeof(vertex_position_uv_normal);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = vertices.data();
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

    gpu::buffer_descriptor index_descriptor{};
    index_descriptor.size = indices.size() * sizeof(uint32_t);
    index_descriptor.usage = gpu::buffer_usage_index;
    index_descriptor.hint = gpu::buffer_usage_hint::static_data;
    index_descriptor.initial_data = indices.data();
    m_index_buffer = gpu.create_buffer(index_descriptor);
}

void rendering_engine::cubed_sphere::render(gpu::render_pass_encoder& encoder)
{
    auto* renderer = rendering_engine::renderer::get_current_renderer();
    if (renderer == nullptr)
    {
        LOG_WRN("Attempted to render cubed_sphere without renderer attached");
        return;
    }
    if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
    {
        LOG_WRN("cubed_sphere::render: renderable not uploaded");
        return;
    }

    auto& gpu = *control::current_engine().gpu;

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = renderer->draw_bind_group_layout();
        gpu::binding_value model_slot{};
        model_slot.binding = 0;
        model_slot.kind = gpu::binding_kind::mat4_value;
        bg_descriptor.entries.push_back(model_slot);
        m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
    }

    std::vector<gpu::binding_value> entries;
    entries.reserve(1);
    gpu::binding_value model_slot{};
    model_slot.binding = 0;
    model_slot.kind = gpu::binding_kind::mat4_value;
    model_slot.mat4_value = transform.get_transform_matrix();
    entries.push_back(model_slot);
    gpu.update_bind_group(m_draw_bind_group, entries);

    encoder.set_vertex_buffer(0, m_vertex_buffer, 0, m_vertex_stride);
    encoder.set_index_buffer(m_index_buffer, gpu::index_format::uint32);
    encoder.set_bind_group(renderer->draw_bind_group_slot(), m_draw_bind_group);
    encoder.draw_indexed(m_index_count, 0);
}

rendering_engine::gpu::buffer rendering_engine::cubed_sphere::get_vertex_buffer() const
{
    return m_vertex_buffer;
}

rendering_engine::gpu::buffer rendering_engine::cubed_sphere::get_index_buffer() const
{
    return m_index_buffer;
}

unsigned int rendering_engine::cubed_sphere::get_index_count() const
{
    return m_index_count;
}
