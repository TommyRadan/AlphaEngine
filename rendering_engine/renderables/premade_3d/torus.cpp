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

#include <rendering_engine/renderables/premade_3d/torus.hpp>

#include <cmath>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>

rendering_engine::torus::torus(
    material* mat, float radius, float tube, unsigned int radial_segments, unsigned int tubular_segments, float arc)
    : m_material{mat}, m_radius{radius}, m_tube{tube}, m_radial_segments{radial_segments},
      m_tubular_segments{tubular_segments}, m_arc{arc}
{
}

rendering_engine::torus::~torus()
{
    auto& gpu = *control::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
    if (m_draw_ubo.valid())
    {
        gpu.destroy(m_draw_ubo);
        m_draw_ubo = {};
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

void rendering_engine::torus::upload()
{
    // One extra ring/column of vertices so UVs and the seam wrap cleanly,
    // matching the Three.js TorusGeometry layout.
    const unsigned int rings = m_radial_segments + 1;
    const unsigned int columns = m_tubular_segments + 1;

    std::vector<vertex_position_uv_normal> vertices;
    vertices.reserve(rings * columns);

    constexpr float pi = 3.14159265358979323846f;

    for (unsigned int j = 0; j < rings; ++j)
    {
        const float v = static_cast<float>(j) / static_cast<float>(m_radial_segments) * 2.0f * pi;
        const float sin_v = std::sin(v);
        const float cos_v = std::cos(v);

        for (unsigned int i = 0; i < columns; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(m_tubular_segments) * m_arc;
            const float sin_u = std::sin(u);
            const float cos_u = std::cos(u);

            vertex_position_uv_normal vertex;
            vertex.pos = infrastructure::math::vec3{
                (m_radius + m_tube * cos_v) * cos_u, (m_radius + m_tube * cos_v) * sin_u, m_tube * sin_v};

            // Normal points from the centre of the tube cross-section out to
            // the surface point; for a torus this is the unit vector
            // (cos_v*cos_u, cos_v*sin_u, sin_v).
            const infrastructure::math::vec3 center{m_radius * cos_u, m_radius * sin_u, 0.0f};
            vertex.normal = infrastructure::math::normalize(vertex.pos - center);

            vertex.uv = infrastructure::math::vec2{static_cast<float>(i) / static_cast<float>(m_tubular_segments),
                                                   static_cast<float>(j) / static_cast<float>(m_radial_segments)};
            vertices.push_back(vertex);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(m_radial_segments * m_tubular_segments * 6);

    for (unsigned int j = 0; j < m_radial_segments; ++j)
    {
        for (unsigned int i = 0; i < m_tubular_segments; ++i)
        {
            const uint32_t a = j * columns + i;
            const uint32_t b = a + 1;
            const uint32_t c = a + columns;
            const uint32_t d = c + 1;

            // CCW winding when viewed from outside the torus, matching the
            // sphere convention: a -> c -> d then a -> d -> b traces a CCW
            // loop in screen space when the outward normal faces the camera.
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);

            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
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

void rendering_engine::torus::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("torus::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
    {
        return;
    }

    auto& gpu = *control::current_engine().gpu;

    if (!m_draw_ubo.valid())
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(infrastructure::math::mat4);
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_draw_ubo = gpu.create_buffer(ubo_descriptor);
    }

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_material->per_draw_layout();
        gpu::binding_value model_slot{};
        model_slot.binding = 1;
        model_slot.kind = gpu::binding_kind::uniform_buffer;
        model_slot.buffer_value = m_draw_ubo;
        bg_descriptor.entries.push_back(model_slot);
        m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
    }

    const auto model_matrix = transform.get_transform_matrix();
    gpu.write_buffer(m_draw_ubo, model_matrix.data(), sizeof(infrastructure::math::mat4), 0);

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.index_buffer = m_index_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}

rendering_engine::gpu::buffer rendering_engine::torus::get_vertex_buffer() const
{
    return m_vertex_buffer;
}

rendering_engine::gpu::buffer rendering_engine::torus::get_index_buffer() const
{
    return m_index_buffer;
}

unsigned int rendering_engine::torus::get_index_count() const
{
    return m_index_count;
}
