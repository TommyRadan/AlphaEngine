// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device_render_target.cpp
 * @brief @c vk_device member functions that manage render targets: the
 *        attachments a target allocates or imports. The render passes
 *        and framebuffers it is drawn through are vk_render_pass_cache's.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <algorithm>
#include <vector>

#include <core/log.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    render_target vk_device::create_render_target(const render_target_descriptor& descriptor)
    {
        if (const char* problem = validate_render_target_descriptor(descriptor); problem != nullptr)
        {
            LOG_ERR("create_render_target: %s", problem);
            return {};
        }
        if (descriptor.color.size() > m_limits.max_color_attachments)
        {
            LOG_ERR("create_render_target: %zu colour attachments, the device allows %u",
                    descriptor.color.size(),
                    m_limits.max_color_attachments);
            return {};
        }
        const sample_count_mask sample_counts =
            descriptor.color.empty()
                ? m_limits.depth_sample_counts
                : (m_limits.color_sample_counts & (descriptor.with_depth ? m_limits.depth_sample_counts : ~0u));
        if (!sample_count_supported(sample_counts, descriptor.sample_count))
        {
            LOG_ERR("create_render_target: %u samples per pixel are not supported for these attachments",
                    descriptor.sample_count);
            return {};
        }

        vk_render_target record{};
        record.is_swapchain = false;
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.samples = descriptor.sample_count;
        record.has_depth = descriptor.with_depth;

        // Textures allocated so far, released if a later step fails so
        // nothing half-built is handed out.
        std::vector<texture> allocated;
        const auto release_allocated = [&]
        {
            for (const texture t : allocated)
            {
                destroy(t);
            }
        };

        // Resolve one attachment: check an imported texture against the
        // target, or allocate a fresh single-mip texture of the target's
        // shape (colour attachments sample linearly, depth attachments
        // nearest, both clamped so a fullscreen pass never wraps at the
        // seam). The render pass and framebuffers are built lazily by
        // acquire_render_pass, per load-op combination.
        const auto resolve = [&](const attachment_desc& desc, bool depth, vk_attachment& out) -> bool
        {
            if (desc.texture.valid())
            {
                const vk_texture* tex = m_textures.lookup(desc.texture.id);
                if (tex == nullptr || tex->image == VK_NULL_HANDLE)
                {
                    LOG_ERR("create_render_target: imported attachment is not a live texture");
                    return false;
                }
                if ((tex->usage & texture_usage_render_attachment) == 0u)
                {
                    LOG_ERR("create_render_target: imported texture was created without "
                            "texture_usage_render_attachment");
                    return false;
                }
                if (tex->is_depth != depth)
                {
                    LOG_ERR("create_render_target: imported texture format does not fit a %s attachment",
                            depth ? "depth" : "colour");
                    return false;
                }
                if (desc.mip_level >= tex->mip_levels || desc.layer >= tex->array_layers)
                {
                    LOG_ERR("create_render_target: level %u / layer %u is outside the imported texture (%u levels, "
                            "%u layers)",
                            desc.mip_level,
                            desc.layer,
                            tex->mip_levels,
                            tex->array_layers);
                    return false;
                }
                if (tex->samples != descriptor.sample_count)
                {
                    LOG_ERR("create_render_target: imported texture has %u samples, the target %u",
                            tex->samples,
                            descriptor.sample_count);
                    return false;
                }
                const uint32_t level_width = std::max(1u, tex->width >> desc.mip_level);
                const uint32_t level_height = std::max(1u, tex->height >> desc.mip_level);
                if (level_width != descriptor.width || level_height != descriptor.height)
                {
                    LOG_ERR("create_render_target: imported level measures %ux%u, the target %ux%u",
                            level_width,
                            level_height,
                            descriptor.width,
                            descriptor.height);
                    return false;
                }
                out.tex = desc.texture;
                out.owned = false;
                out.mip_level = desc.mip_level;
                out.layer = desc.layer;
                return true;
            }

            texture_descriptor td{};
            td.dimension = descriptor.dimension;
            td.format = desc.format;
            td.width = descriptor.width;
            td.height = descriptor.height;
            td.array_layers = descriptor.array_layers;
            td.sample_count = descriptor.sample_count;
            td.mipmaps = false;
            td.usage = texture_usage_default | texture_usage_render_attachment;
            td.min_filter = depth ? filter_mode::nearest : filter_mode::linear;
            td.mag_filter = td.min_filter;
            td.mipmap_filter = mipmap_mode::none;
            td.address_u = address_mode::clamp_edge;
            td.address_v = address_mode::clamp_edge;
            td.address_w = address_mode::clamp_edge;
            const texture t = create_texture(td);
            if (!t.valid())
            {
                return false;
            }
            allocated.push_back(t);
            out.tex = t;
            out.owned = true;
            out.mip_level = 0;
            out.layer = desc.layer;
            return true;
        };

        record.color.resize(descriptor.color.size());
        for (size_t i = 0; i < descriptor.color.size(); ++i)
        {
            if (!resolve(descriptor.color[i], false, record.color[i]))
            {
                release_allocated();
                return {};
            }
        }
        if (descriptor.with_depth)
        {
            if (!resolve(descriptor.depth, true, record.depth))
            {
                release_allocated();
                return {};
            }
            if (const vk_texture* depth_tex = m_textures.lookup(record.depth.tex.id))
            {
                record.has_stencil = (depth_tex->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u;
            }
        }

        render_target h{};
        h.id = m_render_targets.insert(record);
        return h;
    }

    void vk_device::destroy(render_target handle)
    {
        if (handle.id == m_swapchain_target.id)
        {
            return;
        }
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            // The variants own framebuffers and render-pass objects
            // that may still be referenced by the previous frame's
            // command buffer, and pipelines were built against those
            // passes; retire them together through the deferred queue.
            m_render_passes.retire_render_pass_variants(*record, /*device_idle=*/false);
            // Only the attachments the target allocated go with it; an
            // imported texture stays with its owner.
            for (vk_attachment& attachment : record->color)
            {
                if (attachment.owned && attachment.tex.valid())
                {
                    destroy(attachment.tex);
                }
                attachment = {};
            }
            if (record->depth.owned && record->depth.tex.valid())
            {
                destroy(record->depth.tex);
            }
            record->depth = {};
            m_render_targets.remove(handle.id);
        }
    }

    texture vk_device::render_target_color_texture(render_target handle, uint32_t index)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            if (index < record->color.size())
            {
                return record->color[index].tex;
            }
        }
        return {};
    }

    texture vk_device::render_target_depth_texture(render_target handle)
    {
        if (auto* record = m_render_targets.lookup(handle.id))
        {
            return record->depth.tex;
        }
        return {};
    }
} // namespace rendering_engine::gpu::backend::vulkan
