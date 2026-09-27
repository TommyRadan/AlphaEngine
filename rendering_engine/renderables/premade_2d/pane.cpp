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

#include <cstddef>

#include <rendering_engine/gpu/device.hpp>
#include <runtime/engine.hpp>

rendering_engine::pane::pane(ui_material* mat, const core::math::vec2& size)
    : m_batch{mat},
      m_rect{rect_transform::anchored(ui_anchor::top_left, ui_anchor::top_left, core::math::vec2{0.0f, 0.0f}, size)}
{
}

rendering_engine::pane::~pane()
{
    release_owned_texture();
}

void rendering_engine::pane::release_owned_texture()
{
    if (m_owns_texture && m_texture.valid())
    {
        runtime::current_engine().gpu->destroy(m_texture);
    }
    m_texture = {};
    m_owns_texture = false;
}

void rendering_engine::pane::set_color(const rendering_engine::color& color)
{
    m_color = color;
    m_dirty = true;
}

const rendering_engine::color& rendering_engine::pane::get_color() const
{
    return m_color;
}

void rendering_engine::pane::set_image(const rendering_engine::image& image, gpu::color_space space)
{
    // The batch still names the old texture until the next collect
    // rebuilds the quad; it draws nothing with it in between, and the
    // device defers the release past any frame still reading it.
    release_owned_texture();

    gpu::texture_descriptor descriptor{};
    descriptor.dimension = gpu::texture_dimension::d2;
    descriptor.format = gpu::rgba8_format(space);
    descriptor.width = image.get_width();
    descriptor.height = image.get_height();
    descriptor.mipmaps = true;
    descriptor.min_filter = gpu::filter_mode::linear;
    descriptor.mag_filter = gpu::filter_mode::linear;
    descriptor.address_u = gpu::address_mode::clamp_edge;
    descriptor.address_v = gpu::address_mode::clamp_edge;
    descriptor.address_w = gpu::address_mode::clamp_edge;

    auto& gpu = *runtime::current_engine().gpu;
    m_texture = gpu.create_texture(descriptor);
    const std::size_t pixel_bytes =
        static_cast<std::size_t>(image.get_width()) * static_cast<std::size_t>(image.get_height()) * sizeof(color);
    gpu.write_texture(m_texture, image.get_pixels(), pixel_bytes);
    gpu.generate_mipmaps(m_texture);
    m_owns_texture = true;
    m_uv_min = core::math::vec2{0.0f, 0.0f};
    m_uv_max = core::math::vec2{1.0f, 1.0f};
    m_dirty = true;
}

void rendering_engine::pane::set_texture(gpu::texture texture,
                                         const core::math::vec2& uv_min,
                                         const core::math::vec2& uv_max)
{
    release_owned_texture();
    m_texture = texture;
    m_uv_min = uv_min;
    m_uv_max = uv_max;
    m_dirty = true;
}

const rendering_engine::rect_transform& rendering_engine::pane::get_rect() const
{
    return m_rect;
}

void rendering_engine::pane::set_rect(const rect_transform& rect)
{
    m_rect = rect;
    m_dirty = true;
}

void rendering_engine::pane::set_position(const core::math::vec2& position)
{
    m_rect.position = position;
    m_dirty = true;
}

void rendering_engine::pane::set_size(const core::math::vec2& size)
{
    m_rect.size = size;
    m_dirty = true;
}

void rendering_engine::pane::set_anchor(const core::math::vec2& anchor)
{
    m_rect.anchor_min = anchor;
    m_rect.anchor_max = anchor;
    m_dirty = true;
}

void rendering_engine::pane::set_pivot(const core::math::vec2& pivot)
{
    m_rect.pivot = pivot;
    m_dirty = true;
}

void rendering_engine::pane::set_rotation(float radians)
{
    m_rect.rotation = radians;
    m_dirty = true;
}

bool rendering_engine::pane::contains(const core::math::vec2& point) const
{
    return m_rect.contains(drawable_rect(), point);
}

void rendering_engine::pane::upload() {}

void rendering_engine::pane::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_dirty)
    {
        m_batch.clear();
        m_batch.add(m_texture, m_rect, m_color, m_uv_min, m_uv_max);
        m_dirty = false;
    }
    m_batch.collect_draw_items(out);
}
