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

#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/renderers/renderer.hpp>

rendering_engine::pane::pane(const infrastructure::math::vec2& size) : m_size{size} {}

rendering_engine::pane::~pane()
{
    auto& gpu = *control::current_engine().gpu;
    if (m_draw_bind_group.valid())
    {
        gpu.destroy(m_draw_bind_group);
        m_draw_bind_group = {};
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
    auto& gpu = *control::current_engine().gpu;
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
}

void rendering_engine::pane::upload()
{
    m_vertex_count = 6;
    m_vertex_stride = sizeof(vertex_position_uv);

    const infrastructure::math::vec3 position{transform.get_position()};

    vertex_position_uv vertex[4];
    vertex[0].pos = infrastructure::math::vec3{position.x, position.y - m_size.y, -1.0f};
    vertex[0].uv = infrastructure::math::vec2{0.0f, 0.0f};
    vertex[1].pos = infrastructure::math::vec3{position.x + m_size.x, position.y - m_size.y, -1.0f};
    vertex[1].uv = infrastructure::math::vec2{1.0f, 0.0f};
    vertex[2].pos = infrastructure::math::vec3{position.x + m_size.x, position.y, -1.0f};
    vertex[2].uv = infrastructure::math::vec2{1.0f, 1.0f};
    vertex[3].pos = infrastructure::math::vec3{position.x, position.y, -1.0f};
    vertex[3].uv = infrastructure::math::vec2{0.0f, 1.0f};

    const uint32_t indices[6] = {3, 0, 1, 3, 1, 2};

    auto& gpu = *control::current_engine().gpu;

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

void rendering_engine::pane::render(gpu::render_pass_encoder& encoder)
{
    auto* renderer = rendering_engine::renderer::get_current_renderer();
    if (renderer == nullptr)
    {
        LOG_WRN("Attempted to render pane without renderer attached");
        return;
    }
    if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
    {
        return;
    }

    auto& gpu = *control::current_engine().gpu;

    if (!m_draw_bind_group.valid())
    {
        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = renderer->draw_bind_group_layout();
        gpu::binding_value use_texture_slot{};
        use_texture_slot.binding = 0;
        use_texture_slot.kind = gpu::binding_kind::float_value;
        gpu::binding_value color_slot{};
        color_slot.binding = 1;
        color_slot.kind = gpu::binding_kind::vec4_value;
        gpu::binding_value tex_slot{};
        tex_slot.binding = 2;
        tex_slot.kind = gpu::binding_kind::texture;
        bg_descriptor.entries.push_back(use_texture_slot);
        bg_descriptor.entries.push_back(color_slot);
        bg_descriptor.entries.push_back(tex_slot);
        m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
    }

    std::vector<gpu::binding_value> entries;
    entries.reserve(3);

    gpu::binding_value use_texture_value{};
    use_texture_value.binding = 0;
    use_texture_value.kind = gpu::binding_kind::float_value;
    use_texture_value.float_value = m_texture.valid() ? 1.0f : 0.0f;
    entries.push_back(use_texture_value);

    gpu::binding_value color_value{};
    color_value.binding = 1;
    color_value.kind = gpu::binding_kind::vec4_value;
    color_value.vec4_value = infrastructure::math::vec4{static_cast<float>(m_color.r) / 255.0f,
                                                        static_cast<float>(m_color.g) / 255.0f,
                                                        static_cast<float>(m_color.b) / 255.0f,
                                                        static_cast<float>(m_color.a) / 255.0f};
    entries.push_back(color_value);

    gpu::binding_value tex_value{};
    tex_value.binding = 2;
    tex_value.kind = gpu::binding_kind::texture;
    tex_value.texture_value = m_texture;
    entries.push_back(tex_value);

    gpu.update_bind_group(m_draw_bind_group, entries);

    encoder.set_vertex_buffer(0, m_vertex_buffer, 0, m_vertex_stride);
    encoder.set_index_buffer(m_index_buffer, gpu::index_format::uint32);
    encoder.set_bind_group(renderer->draw_bind_group_slot(), m_draw_bind_group);
    encoder.draw_indexed(m_vertex_count, 0);
}
