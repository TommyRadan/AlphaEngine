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

#include <rendering_engine/renderables/premade_2d/pane.hpp>

#include <array>

#include <core/log.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

namespace
{
    // std140 layout for the ui_material per-draw UBO. @c useTexture
    // is a single float at offset 0; @c color is a vec4 at offset 16
    // because std140 rounds vec4 alignment up to 16. Total size 32.
    constexpr size_t pane_draw_ubo_size = 32;
} // namespace

rendering_engine::pane::pane(material* mat, const core::math::vec2& size) : m_material{mat}, m_size{size} {}

rendering_engine::pane::~pane()
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
    if (m_texture.valid())
    {
        gpu.destroy(m_texture);
        m_texture = {};
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

void rendering_engine::pane::set_color(const rendering_engine::util::color& color)
{
    m_color = color;
}

void rendering_engine::pane::set_image(const rendering_engine::util::image& image)
{
    auto& gpu = *runtime::current_engine().gpu;
    if (m_texture.valid())
    {
        gpu.destroy(m_texture);
        m_texture = {};
    }

    gpu::texture_descriptor descriptor{};
    descriptor.dimension = gpu::texture_dimension::d2;
    descriptor.format = gpu::texture_format::rgba8_unorm;
    descriptor.width = image.get_width();
    descriptor.height = image.get_height();
    descriptor.mipmaps = true;
    descriptor.min_filter = gpu::filter_mode::nearest;
    descriptor.mag_filter = gpu::filter_mode::nearest;
    descriptor.address_u = gpu::address_mode::clamp_edge;
    descriptor.address_v = gpu::address_mode::clamp_edge;
    descriptor.address_w = gpu::address_mode::clamp_edge;
    m_texture = gpu.create_texture(descriptor);

    const size_t pixel_bytes =
        static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
    gpu.write_texture(m_texture, image.get_pixels(), pixel_bytes);
    gpu.generate_mipmaps(m_texture);

    // The bind group caches the texture handle at create time, so a
    // late set_image call needs the bind group rebuilt before the
    // next draw. Drop it here; collect_draw_items recreates it.
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
    }
}

void rendering_engine::pane::upload()
{
    m_vertex_count = 6;
    m_vertex_stride = sizeof(vertex_position_uv);

    const core::math::vec3 position{transform.get_position()};

    vertex_position_uv vertex[4];
    vertex[0].pos = core::math::vec3{position.x, position.y - m_size.y, -1.0f};
    vertex[0].uv = core::math::vec2{0.0f, 0.0f};
    vertex[1].pos = core::math::vec3{position.x + m_size.x, position.y - m_size.y, -1.0f};
    vertex[1].uv = core::math::vec2{1.0f, 0.0f};
    vertex[2].pos = core::math::vec3{position.x + m_size.x, position.y, -1.0f};
    vertex[2].uv = core::math::vec2{1.0f, 1.0f};
    vertex[3].pos = core::math::vec3{position.x, position.y, -1.0f};
    vertex[3].uv = core::math::vec2{0.0f, 1.0f};

    const uint32_t indices[6] = {3, 0, 1, 3, 1, 2};

    auto& gpu = *runtime::current_engine().gpu;

    gpu::buffer_descriptor vertex_descriptor{};
    vertex_descriptor.size = sizeof(vertex);
    vertex_descriptor.usage = gpu::buffer_usage_vertex;
    vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
    vertex_descriptor.initial_data = vertex;
    if (m_vertex_buffer.valid())
    {
        gpu.destroy(m_vertex_buffer);
    }
    m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

    gpu::buffer_descriptor index_descriptor{};
    index_descriptor.size = sizeof(indices);
    index_descriptor.usage = gpu::buffer_usage_index;
    index_descriptor.hint = gpu::buffer_usage_hint::static_data;
    index_descriptor.initial_data = indices;
    if (m_index_buffer.valid())
    {
        gpu.destroy(m_index_buffer);
    }
    m_index_buffer = gpu.create_buffer(index_descriptor);
}

void rendering_engine::pane::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("pane::collect_draw_items: no material");
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
        ubo_descriptor.size = pane_draw_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_draw_ubo = gpu.create_buffer(ubo_descriptor);
    }

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_material->per_draw_layout();

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = 0;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_draw_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        gpu::binding_value tex_slot{};
        tex_slot.binding = 1;
        tex_slot.kind = gpu::binding_kind::texture;
        tex_slot.texture_value = m_texture;
        bg_descriptor.entries.push_back(tex_slot);

        m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
    }

    // std140-packed payload: float useTexture; vec4 color (offset 16).
    std::array<float, 8> ubo_payload{};
    ubo_payload[0] = m_texture.valid() ? 1.0f : 0.0f;
    ubo_payload[4] = static_cast<float>(m_color.r) / 255.0f;
    ubo_payload[5] = static_cast<float>(m_color.g) / 255.0f;
    ubo_payload[6] = static_cast<float>(m_color.b) / 255.0f;
    ubo_payload[7] = static_cast<float>(m_color.a) / 255.0f;
    gpu.write_buffer(m_draw_ubo, ubo_payload.data(), pane_draw_ubo_size, 0);

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_vertex_buffer;
    item.index_buffer = m_index_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.index_count = m_vertex_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}
