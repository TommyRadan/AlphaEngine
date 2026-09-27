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

#include <rendering_engine/renderables/points.hpp>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <runtime/engine.hpp>

rendering_engine::points::points(material* mat) : m_material{mat} {}

rendering_engine::points::~points()
{
    auto& gpu = *runtime::current_engine().gpu;
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
    }
}

void rendering_engine::points::set_positions(const std::vector<core::math::vec3>& positions)
{
    m_vertices.clear();
    m_vertices.reserve(positions.size());
    for (const auto& position : positions)
    {
        m_vertices.push_back({position, core::math::vec3{1.0f, 1.0f, 1.0f}});
    }
}

void rendering_engine::points::set_positions(const std::vector<core::math::vec3>& positions,
                                             const std::vector<core::math::vec3>& colors)
{
    if (positions.size() != colors.size())
    {
        LOG_WRN("points::set_positions: positions (%zu) and colors (%zu) size mismatch; ignoring",
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

void rendering_engine::points::upload()
{
    m_vertex_count = m_vertices.size();
    m_vertex_stride = sizeof(vertex_position_color);

    // Box the staged points once per upload so world_bounds is a matrix
    // transform per frame; an empty upload leaves the cloud unbounded.
    const auto bounds =
        compute_position_bounds(m_vertices.data(), m_vertices.size() * sizeof(vertex_position_color), m_vertex_stride);
    m_has_local_bounds = bounds.has_value();
    m_local_bounds = bounds.value_or(core::math::aabb{});

    auto& gpu = *runtime::current_engine().gpu;

    // Re-uploading replaces the previous buffer, so drop it first.
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
        m_vertex_buffer = {};
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
}

bool rendering_engine::points::world_bounds(core::math::aabb& out) const
{
    if (!m_has_local_bounds)
    {
        return false;
    }
    out = core::math::transform(m_local_bounds, transform.get_world_matrix());
    return true;
}

void rendering_engine::points::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("points::collect_draw_items: no material");
        return;
    }
    if (!m_vertex_buffer.valid())
    {
        return;
    }

    // Non-indexed point-list draw: an invalid index buffer tells the
    // pass to call draw(vertex_count). The point topology is baked into
    // the material's pipeline.
    draw_item item{};
    item.mat = m_material;
    // The model + normal matrix, pushed or put in this frame's per-draw
    // ring slot (recomputed only when the transform moved); a
    // mirroring transform flags the item so the pass draws it with
    // the clockwise-front-face variant.
    if (!m_per_draw.bind(transform, m_material->per_draw_layout(), item))
    {
        return;
    }
    item.vertex_buffer = m_vertex_buffer;
    item.vertex_count = static_cast<uint32_t>(m_vertex_count);
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}
