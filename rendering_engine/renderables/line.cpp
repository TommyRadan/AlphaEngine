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

#include <rendering_engine/renderables/line.hpp>

#include <cstdint>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <runtime/engine.hpp>

rendering_engine::line::line(material* mat) : m_material{mat} {}

rendering_engine::line::~line()
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

void rendering_engine::line::set_mode(line_mode mode)
{
    m_mode = mode;
}

void rendering_engine::line::set_positions(const std::vector<core::math::vec3>& positions)
{
    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (const auto& position : positions)
    {
        m_vertices.push_back({position, core::math::vec3{1.0f, 1.0f, 1.0f}});
    }
}

void rendering_engine::line::set_positions(const std::vector<core::math::vec3>& positions,
                                           const std::vector<core::math::vec3>& colors)
{
    if (positions.size() != colors.size())
    {
        LOG_WRN("line::set_positions: positions (%zu) and colors (%zu) size mismatch; ignoring",
                positions.size(),
                colors.size());
        return;
    }

    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        m_vertices.push_back({positions[i], colors[i]});
    }
}

void rendering_engine::line::upload()
{
    m_vertex_count = m_vertices.size();
    m_vertex_stride = sizeof(vertex_position_color);
    m_index_count = 0;

    auto& gpu = *runtime::current_engine().gpu;

    // Re-uploading replaces the previous buffers, so drop them first.
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
    if (m_index_buffer.valid())
    {
        gpu.destroy(m_index_buffer);
        m_index_buffer = {};
    }

    if (m_vertices.empty())
    {
        return;
    }

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = m_vertices.size() * sizeof(vertex_position_color);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = m_vertices.data();
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

    if (m_mode != line_mode::strip)
    {
        // Segments draw the vertices directly, two per segment; nothing
        // to index. An odd trailing vertex has no partner, so it is
        // dropped from the draw (the buffer still holds it).
        return;
    }

    // A strip joins consecutive vertices, but the backend bakes line-list
    // topology, so expand the polyline into segment pairs: vertex i pairs
    // with i + 1 for every i in [0, count - 1). Fewer than two vertices
    // span no segment.
    if (m_vertices.size() < 2)
    {
        return;
    }

    std::vector<uint32_t> indices;
    indices.reserve((m_vertices.size() - 1) * 2);
    for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(m_vertices.size()); ++i)
    {
        indices.push_back(i);
        indices.push_back(i + 1);
    }
    m_index_count = indices.size();

    gpu::buffer_descriptor index_descriptor{};
    index_descriptor.size = indices.size() * sizeof(uint32_t);
    index_descriptor.usage = gpu::buffer_usage_index;
    index_descriptor.hint = gpu::buffer_usage_hint::static_data;
    index_descriptor.initial_data = indices.data();
    m_index_buffer = gpu.create_buffer(index_descriptor);
}

void rendering_engine::line::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("line::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid())
    {
        return;
    }

    auto& gpu = *runtime::current_engine().gpu;

    if (!m_draw_ubo.valid())
    {
        // Per-draw UBO matches the @c PerDraw block in the line_material
        // vertex shader: a single mat4 modelMatrix packed std140
        // (64 bytes, no padding).
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

    // The line topology is baked into the material's pipeline. A strip
    // carries an index buffer expanding the polyline into segment pairs;
    // segments draw the vertices directly, two per segment (an odd
    // trailing vertex is dropped).
    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.vertex_stride = m_vertex_stride;

    if (m_index_buffer.valid())
    {
        item.index_buffer = m_index_buffer;
        item.index_count = static_cast<uint32_t>(m_index_count);
        item.index_format = gpu::index_format::uint32;
    }
    else
    {
        item.vertex_count = static_cast<uint32_t>(m_vertex_count & ~static_cast<size_t>(1));
    }

    out.push_back(item);
}
