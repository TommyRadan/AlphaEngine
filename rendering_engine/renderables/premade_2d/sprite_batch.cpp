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

#include <rendering_engine/renderables/premade_2d/sprite_batch.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <rendering_engine/gpu/device.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    namespace
    {
        constexpr std::size_t vertices_per_quad = 4;
        constexpr std::size_t indices_per_quad = 6;

        // Smallest buffer a group or the index buffer is created with, in
        // quads; both then double as they grow.
        constexpr std::size_t min_quad_capacity = 16;

        std::size_t grown_capacity(std::size_t current, std::size_t needed)
        {
            std::size_t capacity = std::max(current, min_quad_capacity);
            while (capacity < needed)
            {
                capacity *= 2;
            }
            return capacity;
        }
    } // namespace

    sprite_batch::sprite_batch(ui_material* mat) : m_material{mat} {}

    sprite_batch::~sprite_batch()
    {
        for (texture_group& group : m_groups)
        {
            release(group);
        }
        m_groups.clear();
        if (m_index_buffer.valid())
        {
            runtime::current_engine().gpu->destroy(m_index_buffer);
            m_index_buffer = {};
        }
    }

    void sprite_batch::clear()
    {
        for (texture_group& group : m_groups)
        {
            group.vertices.clear();
            group.dirty = true;
        }
        m_quad_count = 0;
    }

    sprite_batch::texture_group& sprite_batch::group_for(gpu::texture texture)
    {
        for (texture_group& group : m_groups)
        {
            if (group.texture == texture)
            {
                return group;
            }
        }
        texture_group& group = m_groups.emplace_back();
        group.texture = texture;
        return group;
    }

    void sprite_batch::add(gpu::texture texture, const sprite_quad& quad)
    {
        if (!texture.valid() && m_material != nullptr)
        {
            texture = m_material->white_texture();
        }
        texture_group& group = group_for(texture);

        const core::math::vec2 rotation{std::cos(quad.rotation), std::sin(quad.rotation)};
        const auto corner = [&](bool max_x, bool max_y)
        {
            ui_vertex vertex{};
            vertex.pivot_anchor = quad.pivot_anchor;
            vertex.pivot_offset = quad.pivot_offset;
            vertex.corner_anchor = core::math::vec2{max_x ? quad.corner_max_anchor.x : quad.corner_min_anchor.x,
                                                    max_y ? quad.corner_max_anchor.y : quad.corner_min_anchor.y};
            vertex.corner_offset = core::math::vec2{max_x ? quad.corner_max_offset.x : quad.corner_min_offset.x,
                                                    max_y ? quad.corner_max_offset.y : quad.corner_min_offset.y};
            vertex.rotation = rotation;
            vertex.uv = core::math::vec2{max_x ? quad.uv_max.x : quad.uv_min.x, max_y ? quad.uv_max.y : quad.uv_min.y};
            vertex.color = quad.color;
            return vertex;
        };
        // Top-left, top-right, bottom-right, bottom-left; the shared index
        // buffer splits every quad along the 0-2 diagonal.
        group.vertices.push_back(corner(false, false));
        group.vertices.push_back(corner(true, false));
        group.vertices.push_back(corner(true, true));
        group.vertices.push_back(corner(false, true));
        group.dirty = true;
        ++m_quad_count;
    }

    void sprite_batch::add(gpu::texture texture,
                           const rect_transform& rect,
                           const color& color,
                           const core::math::vec2& uv_min,
                           const core::math::vec2& uv_max)
    {
        // Each corner of a rect_transform sits (t - pivot) * resolved size
        // from its pivot, t being 0 at the min edge and 1 at the max edge;
        // the resolved size is the anchor span times the drawable plus
        // rect.size, which splits into the anchor and pixel parts below.
        const core::math::vec2 span = rect.anchor_max - rect.anchor_min;
        const core::math::vec2 lead = -rect.pivot;
        const core::math::vec2 trail = core::math::vec2{1.0f, 1.0f} - rect.pivot;

        sprite_quad quad{};
        quad.pivot_anchor = rect.anchor_min + span * rect.pivot;
        quad.pivot_offset = rect.position;
        quad.corner_min_anchor = lead * span;
        quad.corner_min_offset = lead * rect.size;
        quad.corner_max_anchor = trail * span;
        quad.corner_max_offset = trail * rect.size;
        quad.rotation = rect.rotation;
        quad.uv_min = uv_min;
        quad.uv_max = uv_max;
        quad.color = color;
        add(texture, quad);
    }

    std::size_t sprite_batch::quad_count() const
    {
        return m_quad_count;
    }

    void sprite_batch::upload() {}

    void sprite_batch::release(texture_group& group)
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (group.bind_group.valid())
        {
            gpu.destroy(group.bind_group);
            group.bind_group = {};
        }
        if (group.vertex_buffer.valid())
        {
            gpu.destroy(group.vertex_buffer);
            group.vertex_buffer = {};
        }
        group.capacity = 0;
    }

    void sprite_batch::reserve_indices(std::size_t quads)
    {
        if (quads <= m_index_capacity && m_index_buffer.valid())
        {
            return;
        }
        auto& gpu = *runtime::current_engine().gpu;
        if (m_index_buffer.valid())
        {
            gpu.destroy(m_index_buffer);
        }

        m_index_capacity = grown_capacity(m_index_capacity, quads);
        std::vector<uint32_t> indices;
        indices.reserve(m_index_capacity * indices_per_quad);
        for (std::size_t quad = 0; quad < m_index_capacity; ++quad)
        {
            const auto base = static_cast<uint32_t>(quad * vertices_per_quad);
            for (const uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                indices.push_back(base + corner);
            }
        }

        gpu::buffer_descriptor descriptor{};
        descriptor.size = indices.size() * sizeof(uint32_t);
        descriptor.usage = gpu::buffer_usage_index;
        descriptor.hint = gpu::buffer_usage_hint::static_data;
        descriptor.initial_data = indices.data();
        m_index_buffer = gpu.create_buffer(descriptor);
    }

    void sprite_batch::collect_draw_items(std::vector<draw_item>& out)
    {
        if (m_material == nullptr)
        {
            return;
        }

        // A texture nothing was queued for since the last clear has no
        // draw; let its buffers go rather than keep them for a texture
        // that may already be destroyed.
        for (auto it = m_groups.begin(); it != m_groups.end();)
        {
            if (it->vertices.empty())
            {
                release(*it);
                it = m_groups.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (m_groups.empty())
        {
            return;
        }

        std::size_t largest = 0;
        for (const texture_group& group : m_groups)
        {
            largest = std::max(largest, group.vertices.size() / vertices_per_quad);
        }
        reserve_indices(largest);

        // collect runs inside the frame bracket: the buffers are
        // dynamic_data, so the device writes this frame's copy of each
        // (one per frame in flight) and no frame still drawing reads it.
        auto& gpu = *runtime::current_engine().gpu;
        for (texture_group& group : m_groups)
        {
            const std::size_t quads = group.vertices.size() / vertices_per_quad;
            if (quads > group.capacity || !group.vertex_buffer.valid())
            {
                if (group.vertex_buffer.valid())
                {
                    gpu.destroy(group.vertex_buffer);
                }
                group.capacity = grown_capacity(group.capacity, quads);
                gpu::buffer_descriptor descriptor{};
                descriptor.size = group.capacity * vertices_per_quad * sizeof(ui_vertex);
                descriptor.usage = gpu::buffer_usage_vertex | gpu::buffer_usage_copy_dst;
                descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
                group.vertex_buffer = gpu.create_buffer(descriptor);
                group.dirty = true;
            }
            if (group.dirty)
            {
                gpu.write_buffer(
                    group.vertex_buffer, group.vertices.data(), group.vertices.size() * sizeof(ui_vertex), 0);
                group.dirty = false;
            }
            if (!group.bind_group.valid())
            {
                gpu::bind_group_descriptor descriptor{};
                descriptor.layout = m_material->per_draw_layout();
                gpu::binding_value texture_slot{};
                texture_slot.binding = ui_material::texture_binding;
                texture_slot.kind = gpu::binding_kind::texture;
                texture_slot.texture_value = group.texture;
                descriptor.entries.push_back(texture_slot);
                group.bind_group = gpu.create_bind_group(descriptor);
            }

            draw_item item{};
            item.mat = m_material;
            item.vertex_buffer = group.vertex_buffer;
            item.index_buffer = m_index_buffer;
            item.per_draw_bind_group = group.bind_group;
            item.vertex_count = static_cast<uint32_t>(quads * vertices_per_quad);
            item.index_count = static_cast<uint32_t>(quads * indices_per_quad);
            item.vertex_stride = sizeof(ui_vertex);
            out.push_back(item);
        }
    }
} // namespace rendering_engine
