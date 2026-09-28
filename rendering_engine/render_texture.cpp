// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/render_texture.hpp>

#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/resources/texture_asset.hpp>

namespace rendering_engine
{
    render_texture::render_texture(gpu::device& device, uint32_t width, uint32_t height)
        : m_device{&device}, m_width{width}, m_height{height}
    {
        // The view's final image, in the swapchain's encoding: the post
        // chain's last pass writes it, a material samples it. No depth: the
        // view renders its scene into targets of its own.
        gpu::render_target_descriptor descriptor{};
        descriptor.color = {{gpu::texture_format::rgba8_unorm}};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.with_depth = false;
        m_target = device.create_render_target(descriptor);

        // The target owns its colour attachment, so the asset only points
        // at it.
        m_texture = std::make_shared<texture_asset>(device);
        m_texture->texture = device.render_target_color_texture(m_target);
        m_texture->format = gpu::texture_format::rgba8_unorm;
        m_texture->width = width;
        m_texture->height = height;
        m_texture->owns_texture = false;
    }

    render_texture::~render_texture()
    {
        // The target releases its colour attachment; the device defers the
        // free until the frames that used it have retired.
        if (m_target.valid())
        {
            m_device->destroy(m_target);
        }
    }
} // namespace rendering_engine
