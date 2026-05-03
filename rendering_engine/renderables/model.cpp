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

#include <rendering_engine/renderables/model.hpp>

#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>

rendering_engine::model::model(material* mat) : m_material{mat} {}

rendering_engine::model::~model()
{
    auto& gpu = *control::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
}

void rendering_engine::model::upload_mesh(const rendering_engine::mesh& mesh)
{
    m_vertex_count = mesh.vertex_count();
    m_vertex_stride = sizeof(vertex_position_uv_normal);

    auto& gpu = *control::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = m_vertex_count * sizeof(vertex_position_uv_normal);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = mesh.vertices();
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);
}

void rendering_engine::model::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("model::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid())
    {
        return;
    }

    auto& gpu = *control::current_engine().gpu;

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_material->per_draw_layout();
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

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.vertex_count = static_cast<uint32_t>(m_vertex_count);
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}
