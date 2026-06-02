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

#include <rendering_engine/renderables/premade_3d/ring.hpp>

#include <cmath>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

rendering_engine::ring::ring(material* mat,
                             float inner_radius,
                             float outer_radius,
                             unsigned int theta_segments,
                             unsigned int phi_segments,
                             float theta_start,
                             float theta_length)
    : m_material{mat}, m_inner_radius{inner_radius}, m_outer_radius{outer_radius}, m_theta_segments{theta_segments},
      m_phi_segments{phi_segments}, m_theta_start{theta_start}, m_theta_length{theta_length}
{
}

rendering_engine::ring::~ring()
{
    auto& gpu = *runtime::current_engine().gpu;
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

void rendering_engine::ring::upload()
{
    const unsigned int theta_segments = m_theta_segments < 3 ? 3 : m_theta_segments;
    const unsigned int phi_segments = m_phi_segments < 1 ? 1 : m_phi_segments;

    const unsigned int columns = theta_segments + 1;
    const unsigned int rows = phi_segments + 1;

    std::vector<vertex_position_uv_normal> vertices;
    vertices.reserve(rows * columns);

    const core::math::vec3 normal{0.0f, 0.0f, 1.0f};

    // Radial/angular grid: row j sweeps the radius from inner to outer, column
    // i sweeps the angle from theta_start across theta_length.
    float radius = m_inner_radius;
    const float radius_step = (m_outer_radius - m_inner_radius) / static_cast<float>(phi_segments);

    for (unsigned int j = 0; j < rows; ++j)
    {
        for (unsigned int i = 0; i < columns; ++i)
        {
            const float segment =
                m_theta_start + static_cast<float>(i) / static_cast<float>(theta_segments) * m_theta_length;
            const float x = radius * std::cos(segment);
            const float y = radius * std::sin(segment);

            vertex_position_uv_normal vertex;
            vertex.pos = core::math::vec3{x, y, 0.0f};
            vertex.uv = core::math::vec2{(x / m_outer_radius + 1.0f) / 2.0f, (y / m_outer_radius + 1.0f) / 2.0f};
            vertex.normal = normal;
            vertices.push_back(vertex);
        }

        radius += radius_step;
    }

    std::vector<uint32_t> indices;
    indices.reserve(theta_segments * phi_segments * 6);

    // Two triangles per grid cell, wound CCW when viewed from +Z so the +Z
    // normal faces the camera on the front side.
    for (unsigned int j = 0; j < phi_segments; ++j)
    {
        for (unsigned int i = 0; i < theta_segments; ++i)
        {
            const uint32_t a = j * columns + i;
            const uint32_t b = a + columns;
            const uint32_t c = b + 1;
            const uint32_t d = a + 1;

            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(d);

            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    m_index_count = static_cast<unsigned int>(indices.size());
    m_vertex_stride = sizeof(vertex_position_uv_normal);

    auto& gpu = *runtime::current_engine().gpu;

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

void rendering_engine::ring::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("ring::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
    {
        return;
    }

    auto& gpu = *runtime::current_engine().gpu;

    if (!m_draw_ubo.valid())
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(core::math::mat4);
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

    const auto model_matrix = transform.get_world_matrix();
    gpu.write_buffer(m_draw_ubo, model_matrix.data(), sizeof(core::math::mat4), 0);

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.index_buffer = m_index_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}

rendering_engine::gpu::buffer rendering_engine::ring::get_vertex_buffer() const
{
    return m_vertex_buffer;
}

rendering_engine::gpu::buffer rendering_engine::ring::get_index_buffer() const
{
    return m_index_buffer;
}

unsigned int rendering_engine::ring::get_index_count() const
{
    return m_index_count;
}
