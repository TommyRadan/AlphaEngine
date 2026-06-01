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

#include <rendering_engine/renderables/premade_3d/capsule.hpp>

#include <cmath>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>

rendering_engine::capsule::capsule(
    material* mat, float radius, float length, unsigned int cap_segments, unsigned int radial_segments)
    : m_material{mat}, m_radius{radius}, m_length{length}, m_cap_segments{cap_segments},
      m_radial_segments{radial_segments}
{
}

rendering_engine::capsule::~capsule()
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

void rendering_engine::capsule::upload()
{
    constexpr float pi = 3.14159265358979323846f;
    const float half_pi = 0.5f * pi;
    const float half_length = 0.5f * m_length;

    const unsigned int columns = m_radial_segments + 1;

    // A single seamless ring stack, generated top-to-bottom. Each ring
    // carries its axial position @c y, the radial scale (distance of the
    // ring from the axis), the vertical component of the surface normal,
    // and a parametric position @c v used for the axial UV coordinate.
    // The top hemisphere, the cylindrical body, and the bottom hemisphere
    // share their boundary rings so the surface has no seams.
    struct ring
    {
        float y;
        float radial;
        float nh; // horizontal (XZ) magnitude of the surface normal
        float ny; // vertical (Y) component of the surface normal
        float v;
    };

    std::vector<ring> rings;
    rings.reserve(2 * (m_cap_segments + 1) + 1);

    // Surface arc lengths used to keep the axial UV roughly proportional
    // to real surface distance: a quarter circumference per cap plus the
    // straight body.
    const float cap_arc = half_pi * m_radius;
    const float total_arc = 2.0f * cap_arc + m_length;
    const float inv_total_arc = total_arc > 0.0f ? 1.0f / total_arc : 0.0f;

    // Top hemisphere: latitude sweeps from +pi/2 (north pole) down to 0
    // (equator, where it meets the body).
    for (unsigned int i = 0; i <= m_cap_segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(m_cap_segments);
        const float lat = half_pi * (1.0f - t);
        const float sin_lat = std::sin(lat);
        const float cos_lat = std::cos(lat);

        ring r{};
        r.y = half_length + m_radius * sin_lat;
        r.radial = m_radius * cos_lat;
        r.nh = cos_lat;
        r.ny = sin_lat;
        r.v = (t * cap_arc) * inv_total_arc;
        rings.push_back(r);
    }

    // Cylindrical body: interpolate the axial position from +half_length
    // to -half_length. Radial scale is the full radius and the normal is
    // purely radial (no vertical component). The first body ring coincides
    // with the last top-cap ring, so skip it to avoid a degenerate quad.
    for (unsigned int i = 1; i <= m_radial_segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(m_radial_segments);

        ring r{};
        r.y = half_length - t * m_length;
        r.radial = m_radius;
        r.nh = 1.0f;
        r.ny = 0.0f;
        r.v = (cap_arc + t * m_length) * inv_total_arc;
        rings.push_back(r);
    }

    // Bottom hemisphere: latitude sweeps from 0 (equator) down to -pi/2
    // (south pole). The first bottom-cap ring coincides with the last body
    // ring, so skip it as well.
    for (unsigned int i = 1; i <= m_cap_segments; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(m_cap_segments);
        const float lat = -half_pi * t;
        const float sin_lat = std::sin(lat);
        const float cos_lat = std::cos(lat);

        ring r{};
        r.y = -half_length + m_radius * sin_lat;
        r.radial = m_radius * cos_lat;
        r.nh = cos_lat;
        r.ny = sin_lat;
        r.v = (cap_arc + m_length + (-lat / half_pi) * cap_arc) * inv_total_arc;
        rings.push_back(r);
    }

    const unsigned int row_count = static_cast<unsigned int>(rings.size());

    std::vector<vertex_position_uv_normal> vertices;
    vertices.reserve(row_count * columns);

    for (unsigned int i = 0; i < row_count; ++i)
    {
        const ring& r = rings[i];

        for (unsigned int j = 0; j < columns; ++j)
        {
            const float u = static_cast<float>(j) / static_cast<float>(m_radial_segments);
            const float theta = 2.0f * pi * u;
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            vertex_position_uv_normal vertex;
            vertex.pos = infrastructure::math::vec3{r.radial * cos_theta, r.y, r.radial * sin_theta};

            // The radial (XZ) component of the normal points outward in
            // the same direction as the ring offset; on the caps it is the
            // latitude cosine combined with the vertical (sine) component,
            // on the body it is purely radial (nh == 1, ny == 0). The
            // per-ring components are already unit-length, so the result is
            // a unit normal across every region.
            vertex.normal = infrastructure::math::vec3{r.nh * cos_theta, r.ny, r.nh * sin_theta};

            vertex.uv = infrastructure::math::vec2{u, 1.0f - r.v};
            vertices.push_back(vertex);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve((row_count - 1) * m_radial_segments * 6);

    for (unsigned int i = 0; i + 1 < row_count; ++i)
    {
        for (unsigned int j = 0; j < m_radial_segments; ++j)
        {
            const uint32_t a = i * columns + j;
            const uint32_t b = a + 1;
            const uint32_t c = a + columns;
            const uint32_t d = c + 1;

            // CCW winding when viewed from outside the capsule — matches
            // sphere.cpp: top-left -> bottom-left -> bottom-right traces a
            // CCW loop in screen space when the outward normal points
            // toward the camera.
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

void rendering_engine::capsule::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("capsule::collect_draw_items: no material");
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

    const auto model_matrix = transform.get_world_matrix();
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

rendering_engine::gpu::buffer rendering_engine::capsule::get_vertex_buffer() const
{
    return m_vertex_buffer;
}

rendering_engine::gpu::buffer rendering_engine::capsule::get_index_buffer() const
{
    return m_index_buffer;
}

unsigned int rendering_engine::capsule::get_index_count() const
{
    return m_index_count;
}
