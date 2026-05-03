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

#include <rendering_engine/renderables/premade_3d/cube.hpp>

#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>

rendering_engine::cube::cube(material* mat) : m_material{mat} {}

rendering_engine::cube::~cube()
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

void rendering_engine::cube::upload()
{
    m_vertex_count = 36;
    m_vertex_stride = sizeof(vertex_position_normal);

    vertex_position_normal vertices[8];
    vertices[0].pos = infrastructure::math::vec3{-1.0f, -1.0f, 1.0f};
    vertices[1].pos = infrastructure::math::vec3{1.0f, -1.0f, 1.0f};
    vertices[2].pos = infrastructure::math::vec3{1.0f, 1.0f, 1.0f};
    vertices[3].pos = infrastructure::math::vec3{-1.0f, 1.0f, 1.0f};
    vertices[4].pos = infrastructure::math::vec3{-1.0f, -1.0f, -1.0f};
    vertices[5].pos = infrastructure::math::vec3{1.0f, -1.0f, -1.0f};
    vertices[6].pos = infrastructure::math::vec3{1.0f, 1.0f, -1.0f};
    vertices[7].pos = infrastructure::math::vec3{-1.0f, 1.0f, -1.0f};

    const uint32_t indices[36] = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                                  4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};

    vertex_position_normal expanded_vertices[36];
    for (int i = 0; i < 36; i++)
    {
        expanded_vertices[i].pos = vertices[indices[i]].pos;

        const int sector = i / 6;
        switch (sector)
        {
        case 0:
            expanded_vertices[i].normal = infrastructure::math::vec3{0.0f, 0.0f, 1.0f};
            break;
        case 1:
            expanded_vertices[i].normal = infrastructure::math::vec3{1.0f, 0.0f, 0.0f};
            break;
        case 2:
            expanded_vertices[i].normal = infrastructure::math::vec3{0.0f, 0.0f, -1.0f};
            break;
        case 3:
            expanded_vertices[i].normal = infrastructure::math::vec3{-1.0f, 0.0f, 0.0f};
            break;
        case 4:
            expanded_vertices[i].normal = infrastructure::math::vec3{0.0f, -1.0f, 0.0f};
            break;
        case 5:
            expanded_vertices[i].normal = infrastructure::math::vec3{0.0f, 1.0f, 0.0f};
            break;
        default:
            expanded_vertices[i].normal = infrastructure::math::vec3{0.0f, 0.0f, 0.0f};
        }
    }

    auto& gpu = *control::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = sizeof(expanded_vertices);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = expanded_vertices;
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

    // The bind group is created lazily in @ref collect_draw_items;
    // at upload time the per-draw layout is known via @c m_material.
}

void rendering_engine::cube::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("cube::collect_draw_items: no material");
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
    item.vertex_count = m_vertex_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}
