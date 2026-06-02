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

#include <rendering_engine/renderables/premade_3d/cylinder.hpp>

#include <cmath>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

rendering_engine::cylinder::cylinder(material* mat,
                                     float radius_top,
                                     float radius_bottom,
                                     float height,
                                     unsigned int radial_segments,
                                     unsigned int height_segments,
                                     bool open_ended)
    : m_material{mat}, m_radius_top{radius_top}, m_radius_bottom{radius_bottom}, m_height{height},
      m_radial_segments{radial_segments}, m_height_segments{height_segments}, m_open_ended{open_ended}
{
}

rendering_engine::cylinder::~cylinder()
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

void rendering_engine::cylinder::upload()
{
    constexpr float pi = 3.14159265358979323846f;

    std::vector<vertex_position_uv_normal> vertices;
    std::vector<uint32_t> indices;

    const float half_height = m_height * 0.5f;

    // Torso (side wall). Each row of vertices spans radial_segments + 1
    // columns so the seam UV reaches 1.0; the radius is interpolated
    // linearly between the bottom and top. The normal follows the cone
    // slope: its radial component is unit length in the xz plane and the
    // y component is the slope of the side wall.
    const float slope = (m_radius_bottom - m_radius_top) / m_height;

    for (unsigned int y = 0; y <= m_height_segments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(m_height_segments);
        const float radius = v * (m_radius_top - m_radius_bottom) + m_radius_bottom;

        for (unsigned int x = 0; x <= m_radial_segments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(m_radial_segments);
            const float theta = u * 2.0f * pi;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            vertex_position_uv_normal vertex;
            vertex.pos = core::math::vec3{radius * sin_theta, -v * m_height + half_height, radius * cos_theta};
            vertex.normal = core::math::normalize(core::math::vec3{sin_theta, slope, cos_theta});
            vertex.uv = core::math::vec2{u, 1.0f - v};
            vertices.push_back(vertex);
        }
    }

    const unsigned int torso_columns = m_radial_segments + 1;
    for (unsigned int y = 0; y < m_height_segments; ++y)
    {
        for (unsigned int x = 0; x < m_radial_segments; ++x)
        {
            const uint32_t a = y * torso_columns + x;
            const uint32_t b = (y + 1) * torso_columns + x;
            const uint32_t c = (y + 1) * torso_columns + (x + 1);
            const uint32_t d = y * torso_columns + (x + 1);

            // CCW winding when viewed from outside the side wall.
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(d);

            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    // Caps. A cap whose radius is 0 is skipped, so a top radius of 0
    // collapses the top into the apex of a cone. Each cap is a triangle
    // fan around a dedicated center vertex with a flat (+/-Y) normal.
    const auto generate_cap = [&](bool top)
    {
        const float radius = top ? m_radius_top : m_radius_bottom;
        if (radius <= 0.0f)
        {
            return;
        }

        const float sign = top ? 1.0f : -1.0f;
        const float cap_y = half_height * sign;
        const core::math::vec3 normal{0.0f, sign, 0.0f};

        const uint32_t center_start = static_cast<uint32_t>(vertices.size());

        // A center vertex per radial segment keeps the cap UVs aligned
        // with the rim vertices.
        for (unsigned int x = 0; x < m_radial_segments; ++x)
        {
            vertex_position_uv_normal vertex;
            vertex.pos = core::math::vec3{0.0f, cap_y, 0.0f};
            vertex.normal = normal;
            vertex.uv = core::math::vec2{0.5f, 0.5f};
            vertices.push_back(vertex);
        }

        const uint32_t rim_start = static_cast<uint32_t>(vertices.size());
        for (unsigned int x = 0; x <= m_radial_segments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(m_radial_segments);
            const float theta = u * 2.0f * pi;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            vertex_position_uv_normal vertex;
            vertex.pos = core::math::vec3{radius * sin_theta, cap_y, radius * cos_theta};
            vertex.normal = normal;
            vertex.uv = core::math::vec2{cos_theta * 0.5f + 0.5f, sin_theta * 0.5f * sign + 0.5f};
            vertices.push_back(vertex);
        }

        for (unsigned int x = 0; x < m_radial_segments; ++x)
        {
            const uint32_t center = center_start + x;
            const uint32_t r0 = rim_start + x;
            const uint32_t r1 = rim_start + x + 1;

            // Wind so the cap normal (+/-Y) points outward in CCW order.
            if (top)
            {
                indices.push_back(r0);
                indices.push_back(r1);
                indices.push_back(center);
            }
            else
            {
                indices.push_back(r1);
                indices.push_back(r0);
                indices.push_back(center);
            }
        }
    };

    if (!m_open_ended)
    {
        generate_cap(true);
        generate_cap(false);
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

void rendering_engine::cylinder::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("cylinder::collect_draw_items: no material");
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

rendering_engine::gpu::buffer rendering_engine::cylinder::get_vertex_buffer() const
{
    return m_vertex_buffer;
}

rendering_engine::gpu::buffer rendering_engine::cylinder::get_index_buffer() const
{
    return m_index_buffer;
}

unsigned int rendering_engine::cylinder::get_index_count() const
{
    return m_index_count;
}
