// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_2d/pane.hpp>

#include <cstddef>

#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/resources/texture_formats.hpp>

rendering_engine::pane::pane(gpu::device& device, ui_material* mat, const core::math::vec2& size)
    : m_device{&device}, m_batch{mat},
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
        m_device->destroy(m_texture);
    }
    m_texture = {};
    m_owns_texture = false;
}

void rendering_engine::pane::set_color(const assets::color& color)
{
    m_color = color;
    m_dirty = true;
    changed();
}

const assets::color& rendering_engine::pane::get_color() const
{
    return m_color;
}

void rendering_engine::pane::set_image(const assets::image& image, assets::color_space space)
{
    // The proxy still names the old texture until the next extraction
    // captures the quad again; no frame is drawn in between, and the
    // device defers the release past any frame still reading it.
    release_owned_texture();

    gpu::texture_descriptor descriptor{};
    descriptor.dimension = gpu::texture_dimension::d2;
    descriptor.format = rgba8_format(space);
    descriptor.width = image.get_width();
    descriptor.height = image.get_height();
    descriptor.mipmaps = true;
    descriptor.min_filter = gpu::filter_mode::linear;
    descriptor.mag_filter = gpu::filter_mode::linear;
    descriptor.address_u = gpu::address_mode::clamp_edge;
    descriptor.address_v = gpu::address_mode::clamp_edge;
    descriptor.address_w = gpu::address_mode::clamp_edge;

    auto& gpu = *m_device;
    m_texture = gpu.create_texture(descriptor);
    const std::size_t pixel_bytes = static_cast<std::size_t>(image.get_width()) *
                                    static_cast<std::size_t>(image.get_height()) * sizeof(assets::color);
    gpu.write_texture(m_texture, image.get_pixels(), pixel_bytes);
    gpu.generate_mipmaps(m_texture);
    m_owns_texture = true;
    m_uv_min = core::math::vec2{0.0f, 0.0f};
    m_uv_max = core::math::vec2{1.0f, 1.0f};
    m_dirty = true;
    changed();
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
    changed();
}

const rendering_engine::rect_transform& rendering_engine::pane::get_rect() const
{
    return m_rect;
}

void rendering_engine::pane::set_rect(const rect_transform& rect)
{
    m_rect = rect;
    m_dirty = true;
    changed();
}

void rendering_engine::pane::set_position(const core::math::vec2& position)
{
    m_rect.position = position;
    m_dirty = true;
    changed();
}

void rendering_engine::pane::set_size(const core::math::vec2& size)
{
    m_rect.size = size;
    m_dirty = true;
    changed();
}

void rendering_engine::pane::set_anchor(const core::math::vec2& anchor)
{
    m_rect.anchor_min = anchor;
    m_rect.anchor_max = anchor;
    m_dirty = true;
    changed();
}

void rendering_engine::pane::set_pivot(const core::math::vec2& pivot)
{
    m_rect.pivot = pivot;
    m_dirty = true;
    changed();
}

void rendering_engine::pane::set_rotation(float radians)
{
    m_rect.rotation = radians;
    m_dirty = true;
    changed();
}

bool rendering_engine::pane::contains(const ui_rect& drawable, const core::math::vec2& point) const
{
    return m_rect.contains(drawable, point);
}

rendering_engine::ui_element_data rendering_engine::pane::capture()
{
    if (m_dirty)
    {
        m_batch.clear();
        m_batch.add(m_texture, m_rect, m_color, m_uv_min, m_uv_max);
        m_dirty = false;
    }
    return m_batch.capture();
}
