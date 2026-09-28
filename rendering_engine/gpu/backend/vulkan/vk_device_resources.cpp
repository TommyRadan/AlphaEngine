// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device_resources.cpp
 * @brief @c vk_device member functions over its resource tables: the
 *        handle lookups the encoder resolves through, the placeholder
 *        resources a bind group falls back on, and the release of
 *        every resource still live at quit.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <cstdint>
#include <stdexcept>

namespace rendering_engine::gpu::backend::vulkan
{
    void vk_device::create_default_textures()
    {
        // Opaque white 1x1 placeholders. White is a neutral default for
        // the maps these stand in for (albedo / IBL etc. are gated by the
        // material's uniform flags, so the sampled value is unused — it
        // only has to be a valid resource of the right dimension).
        const uint32_t white = 0xFFFFFFFFu;

        texture_descriptor td2{};
        td2.dimension = texture_dimension::d2;
        td2.format = texture_format::rgba8_unorm;
        td2.width = 1;
        td2.height = 1;
        m_default_texture_2d = create_texture(td2);
        write_texture(m_default_texture_2d, &white, sizeof(white));

        texture_descriptor tdc{};
        tdc.dimension = texture_dimension::cube;
        tdc.format = texture_format::rgba8_unorm;
        tdc.width = 1;
        tdc.height = 1;
        m_default_texture_cube = create_texture(tdc);
        for (uint32_t face = 0; face < 6; ++face)
        {
            write_cube_face(m_default_texture_cube, static_cast<cube_face>(face), &white, sizeof(white));
        }
    }

    texture vk_device::default_texture(texture_dimension dim) const noexcept
    {
        return dim == texture_dimension::cube ? m_default_texture_cube : m_default_texture_2d;
    }

    void vk_device::create_fallback_sampler()
    {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.minFilter = VK_FILTER_LINEAR;
        si.magFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.minLod = 0.0f;
        si.maxLod = VK_LOD_CLAMP_NONE;
        si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        si.maxAnisotropy = 1.0f;
        if (!vk_check(vkCreateSampler(m_device.handle(), &si, nullptr, &m_fallback_sampler),
                      "vkCreateSampler (fallback)"))
        {
            m_fallback_sampler = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateSampler (fallback) failed"};
        }
    }

    void vk_device::destroy_resource_tables()
    {
        m_pipelines.for_each(
            [&](vk_pipeline& p)
            {
                for (auto& v : p.graphics_variants)
                {
                    if (v.object != VK_NULL_HANDLE)
                    {
                        vkDestroyPipeline(m_device.handle(), v.object, nullptr);
                        v.object = VK_NULL_HANDLE;
                    }
                }
                p.graphics_variants.clear();
                if (p.compute_object != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(m_device.handle(), p.compute_object, nullptr);
                    p.compute_object = VK_NULL_HANDLE;
                }
                if (p.layout != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(m_device.handle(), p.layout, nullptr);
                    p.layout = VK_NULL_HANDLE;
                }
            });
        m_bind_group_layouts.for_each(
            [&](vk_bind_group_layout& l)
            {
                if (l.object != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(m_device.handle(), l.object, nullptr);
                    l.object = VK_NULL_HANDLE;
                }
            });
        m_shader_modules.for_each(
            [&](vk_shader_module& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroyShaderModule(m_device.handle(), s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_samplers.for_each(
            [&](vk_sampler& s)
            {
                if (s.object != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device.handle(), s.object, nullptr);
                    s.object = VK_NULL_HANDLE;
                }
            });
        m_textures.for_each(
            [&](vk_texture& t)
            {
                if (t.default_sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(m_device.handle(), t.default_sampler, nullptr);
                    t.default_sampler = VK_NULL_HANDLE;
                }
                if (t.view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_device.handle(), t.view, nullptr);
                    t.view = VK_NULL_HANDLE;
                }
                for (VkImageView storage_view : t.storage_views)
                {
                    if (storage_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device.handle(), storage_view, nullptr);
                    }
                }
                t.storage_views.clear();
                for (VkImageView attachment_view : t.attachment_views)
                {
                    if (attachment_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(m_device.handle(), attachment_view, nullptr);
                    }
                }
                t.attachment_views.clear();
                if (!t.external && t.image != VK_NULL_HANDLE)
                {
                    vmaDestroyImage(m_device.allocator(), t.image, t.allocation);
                }
                t.image = VK_NULL_HANDLE;
                t.allocation = VK_NULL_HANDLE;
            });
        m_buffers.for_each(
            [&](vk_buffer& b)
            {
                // The persistent map belongs to the allocation and goes
                // with it.
                if (b.object != VK_NULL_HANDLE)
                {
                    vmaDestroyBuffer(m_device.allocator(), b.object, b.allocation);
                }
                b.object = VK_NULL_HANDLE;
                b.allocation = VK_NULL_HANDLE;
                b.mapped = nullptr;
            });
        m_render_targets.for_each(
            [&](vk_render_target& rt)
            {
                for (auto& v : rt.variants)
                {
                    for (auto fb : v.framebuffers)
                    {
                        if (fb != VK_NULL_HANDLE)
                        {
                            vkDestroyFramebuffer(m_device.handle(), fb, nullptr);
                        }
                    }
                    v.framebuffers.clear();
                    if (v.render_pass != VK_NULL_HANDLE)
                    {
                        vkDestroyRenderPass(m_device.handle(), v.render_pass, nullptr);
                        v.render_pass = VK_NULL_HANDLE;
                    }
                }
                rt.variants.clear();
            });

        m_pipelines.clear();
        m_shader_modules.clear();
        m_bind_group_layouts.clear();
        m_bind_groups.clear();
        m_samplers.clear();
        m_textures.clear();
        m_buffers.clear();
        m_render_targets.clear();
    }

    vk_buffer* vk_device::lookup_buffer(buffer h)
    {
        return m_buffers.lookup(h.id);
    }
    vk_texture* vk_device::lookup_texture(texture h)
    {
        return m_textures.lookup(h.id);
    }
    vk_sampler* vk_device::lookup_sampler(sampler h)
    {
        return m_samplers.lookup(h.id);
    }
    vk_shader_module* vk_device::lookup_shader_module(shader_module h)
    {
        return m_shader_modules.lookup(h.id);
    }
    vk_pipeline* vk_device::lookup_pipeline(pipeline h)
    {
        return m_pipelines.lookup(h.id);
    }
    vk_bind_group* vk_device::lookup_bind_group(bind_group h)
    {
        return m_bind_groups.lookup(h.id);
    }
    vk_render_target* vk_device::lookup_render_target(render_target h)
    {
        return m_render_targets.lookup(h.id);
    }
    vk_bind_group_layout* vk_device::lookup_bind_group_layout(bind_group_layout h)
    {
        return m_bind_group_layouts.lookup(h.id);
    }
} // namespace rendering_engine::gpu::backend::vulkan
