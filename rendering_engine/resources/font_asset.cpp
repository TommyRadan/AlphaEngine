// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/resources/font_asset.hpp>

#include <cstddef>

#include <assets/color.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine
{
    font_asset::font_asset(gpu::device& device, const std::string& filename, float size)
        : font{filename, size}, m_device{&device}
    {
        const assets::image& pixels = font.atlas();

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::texture_format::rgba8_unorm;
        descriptor.width = pixels.get_width();
        descriptor.height = pixels.get_height();
        descriptor.mipmaps = false;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = gpu::address_mode::clamp_edge;
        descriptor.address_v = gpu::address_mode::clamp_edge;
        descriptor.address_w = gpu::address_mode::clamp_edge;

        atlas = m_device->create_texture(descriptor);
        const std::size_t pixel_bytes = static_cast<std::size_t>(pixels.get_width()) *
                                        static_cast<std::size_t>(pixels.get_height()) * sizeof(assets::color);
        m_device->write_texture(atlas, pixels.get_pixels(), pixel_bytes);
    }

    font_asset::~font_asset()
    {
        // The engine releases every asset handle (the cache and the
        // renderables holding these handles) before it destroys the device.
        if (atlas.valid())
        {
            m_device->destroy(atlas);
        }
    }
} // namespace rendering_engine
