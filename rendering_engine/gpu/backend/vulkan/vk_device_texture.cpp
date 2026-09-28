// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_device_texture.cpp
 * @brief @c vk_device member functions that manage @c VkImage,
 *        @c VkImageView and @c VkSampler objects, the texture uploads
 *        and the synchronous readback.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_negotiate.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        VkSamplerCreateInfo make_sampler_create_info(
            filter_mode min_f, filter_mode mag_f, mipmap_mode mip_f, address_mode u, address_mode v, address_mode w)
        {
            VkSamplerCreateInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            si.minFilter = to_vk_filter(min_f);
            si.magFilter = to_vk_filter(mag_f);
            si.mipmapMode = to_vk_mipmap_mode(mip_f);
            si.addressModeU = to_vk_address_mode(u);
            si.addressModeV = to_vk_address_mode(v);
            si.addressModeW = to_vk_address_mode(w);
            si.minLod = 0.0f;
            si.maxLod = (mip_f == mipmap_mode::none) ? 0.0f : VK_LOD_CLAMP_NONE;
            si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            si.anisotropyEnable = VK_FALSE;
            si.maxAnisotropy = 1.0f;
            return si;
        }

        // A standalone sampler carries the full descriptor: the LOD
        // range and bias, the border colour, anisotropy (gated on the
        // feature and clamped to the device limit by the caller) and
        // the shadow-comparison state the texture-baked sampler never
        // has.
        VkSamplerCreateInfo make_sampler_create_info(const sampler_descriptor& descriptor, float max_anisotropy)
        {
            VkSamplerCreateInfo si = make_sampler_create_info(descriptor.min_filter,
                                                              descriptor.mag_filter,
                                                              descriptor.mipmap,
                                                              descriptor.address_u,
                                                              descriptor.address_v,
                                                              descriptor.address_w);
            si.minLod = descriptor.lod_min_clamp;
            si.maxLod = descriptor.mipmap == mipmap_mode::none ? 0.0f : descriptor.lod_max_clamp;
            si.mipLodBias = descriptor.lod_bias;
            si.borderColor = to_vk_border_color(descriptor.border);
            si.anisotropyEnable = max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            si.maxAnisotropy = max_anisotropy > 1.0f ? max_anisotropy : 1.0f;
            si.compareEnable = descriptor.compare_enabled ? VK_TRUE : VK_FALSE;
            si.compareOp = to_vk_compare(descriptor.compare);
            return si;
        }

        // vkCmdBlitImage with VK_FILTER_LINEAR — the down-sampling
        // primitive @ref vk_device::generate_mipmaps relies on — is only
        // valid when the format advertises blit-src, blit-dst and linear
        // sample filtering with optimal tiling. The engine's mipmapped
        // textures are rgba8_unorm / rgba8_srgb / rgba16_float, which
        // support this on every desktop GPU (all three are mandatory
        // blit + linear-filter formats), but a format that doesn't gets
        // a single level rather than an undefined chain. A blit from an
        // sRGB image decodes to linear before filtering and re-encodes
        // on write, so the sRGB chain is gamma-correct.
        bool format_supports_linear_blit(VkPhysicalDevice physical_device, VkFormat format)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
            const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            return (props.optimalTilingFeatures & required) == required;
        }

        // A texture requested as a storage image only gets the usage bit
        // when the format can actually back one with optimal tiling —
        // otherwise vkCreateImage would fail. rgba16f (the IBL format)
        // is a guaranteed storage format; this guards the general case.
        bool format_supports_storage_image(VkPhysicalDevice physical_device, VkFormat format)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
            return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
        }

        // The pipeline stage + access mask that characterise an image while it
        // sits in a given layout, used to scope a layout-transition barrier to
        // exactly the work it must order rather than ALL_COMMANDS. Used for
        // both sides of the barrier (old layout -> src, new layout -> dst).
        struct layout_sync
        {
            VkPipelineStageFlags stage;
            VkAccessFlags access;
        };

        layout_sync sync_for_layout(VkImageLayout layout)
        {
            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                // Nothing to wait for / flush when discarding prior contents.
                return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT};
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT};
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                // Sampled from either the fragment or a compute shader.
                return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_SHADER_READ_BIT};
            case VK_IMAGE_LAYOUT_GENERAL:
                // Storage-image read/write from compute (the IBL convolution).
                return {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
            default:
                // Unknown layout: fall back to the conservative full barrier.
                return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
            }
        }

        void transition_image(VkCommandBuffer cmd,
                              VkImage image,
                              VkImageAspectFlags aspect,
                              VkImageLayout old_layout,
                              VkImageLayout new_layout,
                              uint32_t mip_levels,
                              uint32_t layer_count)
        {
            const layout_sync src = sync_for_layout(old_layout);
            const layout_sync dst = sync_for_layout(new_layout);

            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = old_layout;
            b.newLayout = new_layout;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange.aspectMask = aspect;
            b.subresourceRange.levelCount = mip_levels;
            b.subresourceRange.layerCount = layer_count;
            b.srcAccessMask = src.access;
            b.dstAccessMask = dst.access;
            vkCmdPipelineBarrier(cmd, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        // The single aspect a buffer <-> image copy or a sampled view
        // of @p tex addresses: colour, or the depth plane of a depth /
        // depth-stencil image (a copy or descriptor names one aspect).
        VkImageAspectFlags single_aspect(const vk_texture& tex)
        {
            return (tex.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                  : VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkImageViewType sampled_view_type(const vk_texture& tex)
        {
            if (tex.is_cube)
            {
                return VK_IMAGE_VIEW_TYPE_CUBE;
            }
            if (tex.is_3d)
            {
                return VK_IMAGE_VIEW_TYPE_3D;
            }
            return tex.is_array ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        }

        // True when @p region addresses texels that exist in @p record;
        // logs the first problem otherwise.
        bool region_fits(const char* where, const vk_texture& record, const texture_copy_region& region)
        {
            if (region.mip_level >= record.mip_levels || region.layer >= record.array_layers)
            {
                LOG_WRN("%s: level %u / layer %u is outside the texture (%u levels, %u layers)",
                        where,
                        region.mip_level,
                        region.layer,
                        record.mip_levels,
                        record.array_layers);
                return false;
            }
            const uint32_t level_width = std::max(1u, record.width >> region.mip_level);
            const uint32_t level_height = std::max(1u, record.height >> region.mip_level);
            const uint32_t level_depth = std::max(1u, record.depth >> region.mip_level);
            if (region.width == 0 || region.height == 0 || region.depth == 0 ||
                static_cast<uint64_t>(region.x) + region.width > level_width ||
                static_cast<uint64_t>(region.y) + region.height > level_height ||
                static_cast<uint64_t>(region.z) + region.depth > level_depth)
            {
                LOG_WRN("%s: %ux%ux%u at %u,%u,%u does not fit level %u (%ux%ux%u)",
                        where,
                        region.width,
                        region.height,
                        region.depth,
                        region.x,
                        region.y,
                        region.z,
                        region.mip_level,
                        level_width,
                        level_height,
                        level_depth);
                return false;
            }
            return true;
        }
    } // namespace

    texture vk_device::create_texture(const texture_descriptor& descriptor)
    {
        const bool multisampled = descriptor.sample_count > 1;
        vk_texture record{};
        record.format = descriptor.format;
        // Depth formats resolve through the device's fallback chains
        // (the packed 24-bit formats are optional); the aspect follows
        // the resolved VkFormat, not the engine format.
        record.vk_format = vk_format_for(descriptor.format);
        record.aspect = aspect_for_vk_format(record.vk_format);
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.depth = (descriptor.dimension == texture_dimension::d3) ? descriptor.depth : 1u;
        record.mipmaps = descriptor.mipmaps;
        record.is_cube = descriptor.dimension == texture_dimension::cube;
        record.is_array = descriptor.dimension == texture_dimension::d2_array;
        record.is_3d = descriptor.dimension == texture_dimension::d3;
        record.is_depth = is_depth_format(descriptor.format);
        record.array_layers = effective_array_layers(descriptor.dimension, descriptor.array_layers);
        record.samples = descriptor.sample_count;
        record.usage = descriptor.usage;
        record.mip_levels = 1;

        if (record.width == 0 || record.height == 0 || record.depth == 0)
        {
            LOG_ERR("create_texture: zero-sized texture (%ux%ux%u)", record.width, record.height, record.depth);
            return {};
        }
        if (multisampled)
        {
            // Only the 2D shapes have a multisampled form, and a
            // multisampled image is an attachment: single-level, never
            // uploaded to, at a count the device offers for its class.
            if (record.is_cube || record.is_3d)
            {
                LOG_ERR("create_texture: only 2D and 2D-array textures can be multisampled");
                return {};
            }
            const sample_count_mask supported =
                record.is_depth ? m_limits.depth_sample_counts : m_limits.color_sample_counts;
            if (!sample_count_supported(supported, descriptor.sample_count))
            {
                LOG_ERR("create_texture: %u samples per texel are not supported for this format",
                        descriptor.sample_count);
                return {};
            }
        }

        // A mipmapped colour texture allocates its whole chain up front so
        // every level is addressable; @ref generate_mipmaps fills levels
        // 1..n via vkCmdBlitImage. Depth targets and 3D textures keep a
        // single level unless a count was asked for explicitly (the
        // engine never requests mips for either, and a 3D blit would also
        // have to halve depth). A full chain requested through @c mipmaps
        // on a format without linear blit support falls back to one
        // level rather than leaving the chain undefined; an explicit
        // count is honoured, and generate_mipmaps refuses the blit.
        record.blit_capable = !record.is_depth && !record.is_3d &&
                              format_supports_linear_blit(m_physical_device.handle(), record.vk_format);
        const uint32_t requested_levels = effective_mip_level_count(descriptor);
        if (requested_levels > 1 && (descriptor.mip_level_count != 0 || record.blit_capable))
        {
            record.mip_levels = requested_levels;
        }
        record.storage_views.assign(record.mip_levels, VK_NULL_HANDLE);
        record.attachment_views.assign(static_cast<size_t>(record.mip_levels) * record.array_layers, VK_NULL_HANDLE);

        // Storage usage is opt-in (@c texture_usage_storage) so the
        // common sampled texture keeps its framebuffer-compression-
        // friendly usage set; only resources bound as storage images
        // (the IBL convolution outputs) pay for the extra bit, and
        // only when the format can back one.
        record.storage = (descriptor.usage & texture_usage_storage) != 0u && !record.is_depth &&
                         format_supports_storage_image(m_physical_device.handle(), record.vk_format);
        texture_usage image_usage = descriptor.usage & ~texture_usage_storage;
        if (record.storage)
        {
            image_usage |= texture_usage_storage;
        }
        // Attachment usage is what the engine's targets ask for; a
        // texture created without it is refused by create_render_target
        // rather than failing inside vkCreateFramebuffer.
        const VkImageUsageFlags usage = to_vk_image_usage(image_usage, record.is_depth);

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = record.is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ii.format = record.vk_format;
        ii.extent = {record.width, record.height, record.depth};
        ii.mipLevels = record.mip_levels;
        ii.arrayLayers = record.array_layers;
        ii.samples = to_vk_sample_count(record.samples);
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.flags = record.is_cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        // Image and memory come from VMA together: device-local, bound,
        // and a sub-allocation of the allocator's blocks unless the
        // driver prefers a dedicated allocation for this image.
        const VmaAllocationCreateInfo alloc = device_local_allocation();
        if (!vk_check(vmaCreateImage(m_device.allocator(), &ii, &alloc, &record.image, &record.allocation, nullptr),
                      "vmaCreateImage"))
        {
            return {};
        }
        // Everything past this point releases the image + memory (and
        // whatever else was built) on failure; nothing half-created is
        // handed out.
        const auto release = [&]
        {
            if (record.default_sampler != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_device.handle(), record.default_sampler, nullptr);
            }
            if (record.view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_device.handle(), record.view, nullptr);
            }
            vmaDestroyImage(m_device.allocator(), record.image, record.allocation);
        };

        // The sampling view names one aspect: the depth plane of a
        // depth-stencil image, since a combined-image-sampler
        // descriptor cannot reference both.
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = record.image;
        vi.viewType = sampled_view_type(record);
        vi.format = record.vk_format;
        vi.subresourceRange.aspectMask = single_aspect(record);
        vi.subresourceRange.levelCount = record.mip_levels;
        vi.subresourceRange.layerCount = record.array_layers;
        if (!vk_check(vkCreateImageView(m_device.handle(), &vi, nullptr, &record.view), "vkCreateImageView"))
        {
            record.view = VK_NULL_HANDLE;
            release();
            return {};
        }

        // effective_mipmap_filter: a mipmapped texture samples its chain
        // even when the descriptor left the filter at none (the shared
        // rule in gpu/texture.hpp), a single-level one never does.
        VkSamplerCreateInfo si =
            make_sampler_create_info(descriptor.min_filter,
                                     descriptor.mag_filter,
                                     effective_mipmap_filter(record.mip_levels > 1, descriptor.mipmap_filter),
                                     descriptor.address_u,
                                     descriptor.address_v,
                                     descriptor.address_w);
        if (!vk_check(vkCreateSampler(m_device.handle(), &si, nullptr, &record.default_sampler),
                      "vkCreateSampler (texture)"))
        {
            // The image is still usable as an attachment and as a copy
            // target; a bind group that samples it substitutes the
            // device's fallback sampler (and says so), so a null
            // sampler never reaches a descriptor.
            record.default_sampler = VK_NULL_HANDLE;
        }

        // The move out of UNDEFINED is recorded into the open transfer
        // batch, which runs ahead of the first command buffer that can
        // attach or sample the image; the record carries the layout it
        // will be in from then on. Every off-screen attachment — depth
        // included — rests in the sampled layout between render passes
        // (see acquire_render_pass), so that is where a texture starts:
        // a pass that loads it, a copy and a readback all find the
        // layout the record says.
        record.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkCommandBuffer cmd = m_transfer.transfer_command_buffer();
        if (cmd == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_device::create_texture: no command buffer for the initial layout transition");
            release();
            return {};
        }
        const VkImageLayout target = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transition_image(cmd,
                         record.image,
                         record.aspect,
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         target,
                         record.mip_levels,
                         record.array_layers);
        record.layout = target;

        texture h{};
        h.id = m_textures.insert(record);
        return h;
    }

    void vk_device::destroy(texture handle)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        const VkDevice dev = m_device.handle();
        const VmaAllocator allocator = m_device.allocator();
        const VkSampler sampler = record->default_sampler;
        const VkImageView view = record->view;
        std::vector<VkImageView> extra_views = std::move(record->storage_views);
        extra_views.insert(extra_views.end(), record->attachment_views.begin(), record->attachment_views.end());
        record->attachment_views.clear();
        const VkImage image = record->external ? VK_NULL_HANDLE : record->image;
        const VmaAllocation allocation = record->allocation;
        // Deferred for the same reason as destroy(buffer): a texture
        // sampled by an in-flight command buffer must outlive the
        // submission that referenced it.
        enqueue_destroy(
            [dev, allocator, sampler, view, extra_views = std::move(extra_views), image, allocation]
            {
                if (sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(dev, sampler, nullptr);
                }
                if (view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(dev, view, nullptr);
                }
                for (VkImageView extra_view : extra_views)
                {
                    if (extra_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(dev, extra_view, nullptr);
                    }
                }
                if (image != VK_NULL_HANDLE)
                {
                    vmaDestroyImage(allocator, image, allocation);
                }
            });
        record->default_sampler = VK_NULL_HANDLE;
        record->view = VK_NULL_HANDLE;
        record->image = VK_NULL_HANDLE;
        record->allocation = VK_NULL_HANDLE;
        m_textures.remove(handle.id);
    }

    namespace
    {
        // Stage @p size bytes of @p data and record their copy into
        // level @p mip_level, layer @p base_layer of @p record at
        // @p offset, of @p extent texels, into the open transfer batch.
        void upload_region(vk_transfer& transfer,
                           vk_texture& record,
                           uint32_t mip_level,
                           uint32_t base_layer,
                           const VkOffset3D& offset,
                           const VkExtent3D& extent,
                           const void* data,
                           size_t size)
        {
            if (data == nullptr || size == 0)
            {
                LOG_ERR("vk_device: texture upload with no data (%zu bytes)", size);
                return;
            }

            // rgb8_unorm images are backed by R8G8B8A8_UNORM (see
            // to_vk_format: R8G8B8 is not a sampled format on most
            // devices), so a 3-byte-per-texel source is widened here
            // with an opaque alpha before it is staged. The size has
            // to match the region exactly for the widening to be
            // meaningful, so a mismatch is refused rather than padded
            // into garbage.
            const void* source = data;
            size_t source_size = size;
            std::vector<uint8_t> padded;
            if (record.format == texture_format::rgb8_unorm)
            {
                const size_t texels = static_cast<size_t>(extent.width) * extent.height * extent.depth;
                if (size != texels * 3)
                {
                    LOG_ERR("vk_device: rgb8_unorm upload is %zu bytes, expected %zu for %ux%ux%u",
                            size,
                            texels * 3,
                            extent.width,
                            extent.height,
                            extent.depth);
                    return;
                }
                padded.resize(texels * 4);
                const auto* in = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < texels; ++i)
                {
                    padded[i * 4 + 0] = in[i * 3 + 0];
                    padded[i * 4 + 1] = in[i * 3 + 1];
                    padded[i * 4 + 2] = in[i * 3 + 2];
                    padded[i * 4 + 3] = 0xFF;
                }
                source = padded.data();
                source_size = padded.size();
            }

            // The bytes go into the staging ring and the copy into the
            // open transfer batch; both transitions bracket it there,
            // in batch order, so record.layout is what the image is in
            // once the batch has run. Nothing waits: the batch is
            // submitted ahead of the first frame that samples the
            // texture and its ring bytes are released by its fence.
            vk_transfer::staged_upload staged{};
            if (!transfer.stage_upload(source, source_size, staged))
            {
                LOG_ERR("vk_device: texture upload of %zu bytes skipped (staging failed)", source_size);
                return;
            }
            transition_image(staged.cmd,
                             record.image,
                             record.aspect,
                             record.layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             record.mip_levels,
                             record.array_layers);

            VkBufferImageCopy region{};
            region.bufferOffset = staged.offset;
            region.imageSubresource.aspectMask = single_aspect(record);
            region.imageSubresource.mipLevel = mip_level;
            region.imageSubresource.layerCount = 1;
            region.imageSubresource.baseArrayLayer = base_layer;
            region.imageOffset = offset;
            region.imageExtent = extent;
            vkCmdCopyBufferToImage(
                staged.cmd, staged.buffer, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            transition_image(staged.cmd,
                             record.image,
                             record.aspect,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             record.mip_levels,
                             record.array_layers);
            record.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    } // namespace

    void vk_device::write_texture(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || record->is_cube || record->is_3d ||
            record->is_array || record->samples > 1)
        {
            return;
        }
        upload_region(m_transfer, *record, 0, 0, {0, 0, 0}, {record->width, record->height, 1}, data, size);
    }

    bool
    vk_device::write_texture_region(texture handle, const texture_write_region& region, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || record->is_3d || record->samples > 1)
        {
            LOG_WRN("write_texture_region: invalid 2D / 2D-array / cube texture handle");
            return false;
        }
        if (region.mip_level >= record->mip_levels)
        {
            LOG_WRN("write_texture_region: level %u of a %u-level texture", region.mip_level, record->mip_levels);
            return false;
        }
        if (region.layer >= record->array_layers)
        {
            LOG_WRN("write_texture_region: layer %u of a %u-layer texture", region.layer, record->array_layers);
            return false;
        }
        const uint32_t level_width = std::max(1u, record->width >> region.mip_level);
        const uint32_t level_height = std::max(1u, record->height >> region.mip_level);
        const uint64_t right = static_cast<uint64_t>(region.x) + region.width;
        const uint64_t bottom = static_cast<uint64_t>(region.y) + region.height;
        if (region.width == 0 || region.height == 0 || right > level_width || bottom > level_height)
        {
            LOG_WRN("write_texture_region: %ux%u at %u,%u does not fit level %u (%ux%u)",
                    region.width,
                    region.height,
                    region.x,
                    region.y,
                    region.mip_level,
                    level_width,
                    level_height);
            return false;
        }
        // The client layout of every upload is the tightly packed
        // storage texel, or whole blocks of a compressed format (rgb8 is
        // widened in upload_region).
        const size_t required = record->format == texture_format::rgb8_unorm
                                    ? static_cast<size_t>(region.width) * region.height * 3u
                                    : texture_image_bytes(record->format, region.width, region.height);
        if (size < required)
        {
            LOG_WRN("write_texture_region: %zu bytes supplied, %zu needed", size, required);
            return false;
        }
        upload_region(m_transfer,
                      *record,
                      region.mip_level,
                      region.layer,
                      {static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), 0},
                      {region.width, region.height, 1},
                      data,
                      required);
        return true;
    }

    void vk_device::write_texture_3d(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || !record->is_3d)
        {
            return;
        }
        upload_region(m_transfer, *record, 0, 0, {0, 0, 0}, {record->width, record->height, record->depth}, data, size);
    }

    void vk_device::write_cube_face(texture handle, cube_face face, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || !record->is_cube)
        {
            return;
        }
        upload_region(m_transfer,
                      *record,
                      0,
                      static_cast<uint32_t>(face),
                      {0, 0, 0},
                      {record->width, record->height, 1},
                      data,
                      size);
    }

    bool vk_device::read_texture(texture handle, const texture_copy_region& region, void* out, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || out == nullptr)
        {
            LOG_WRN("read_texture: invalid texture handle or destination");
            return false;
        }
        if (record->samples > 1)
        {
            LOG_WRN("read_texture: a multisampled texture cannot be read back directly");
            return false;
        }
        if ((record->usage & texture_usage_copy_src) == 0u)
        {
            LOG_WRN("read_texture: the texture was created without texture_usage_copy_src");
            return false;
        }
        if (!region_fits("read_texture", *record, region))
        {
            return false;
        }
        const size_t required = texture_region_bytes(record->format, region);
        if (size < required)
        {
            LOG_WRN("read_texture: %zu bytes supplied, %zu needed", size, required);
            return false;
        }
        if (m_device.device_lost() || m_device.handle() == VK_NULL_HANDLE)
        {
            return false;
        }

        // Synchronous by contract: whatever was queued before — the
        // frame that rendered the texture, the transfer batches — runs
        // ahead of one dedicated submission that copies the region
        // into a host-visible buffer, and the copy is waited on its own
        // fence before the bytes are read. The uploads queued so far go
        // first so a texture written this frame reads back complete.
        m_transfer.flush_transfer_batch();

        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = required;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai = host_mapped_allocation(/*prefer_host=*/true);
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer readback = VK_NULL_HANDLE;
        VmaAllocation readback_allocation = VK_NULL_HANDLE;
        VmaAllocationInfo info{};
        if (!vk_check(vmaCreateBuffer(m_device.allocator(), &bi, &ai, &readback, &readback_allocation, &info),
                      "vmaCreateBuffer (readback)"))
        {
            return false;
        }
        bool ok = false;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        do
        {
            if (info.pMappedData == nullptr)
            {
                LOG_ERR("read_texture: the readback allocation is not mapped");
                break;
            }
            VkCommandBufferAllocateInfo cai{};
            cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cai.commandPool = m_transfer.command_pool();
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            if (!vk_check(vkAllocateCommandBuffers(m_device.handle(), &cai, &cmd),
                          "vkAllocateCommandBuffers (readback)"))
            {
                cmd = VK_NULL_HANDLE;
                break;
            }
            VkFenceCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (!vk_check(vkCreateFence(m_device.handle(), &fi, nullptr, &fence), "vkCreateFence (readback)"))
            {
                fence = VK_NULL_HANDLE;
                break;
            }
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (!vk_check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer (readback)"))
            {
                break;
            }
            // Order the copy behind everything the queue is still
            // executing, then move the image to the transfer layout
            // and back to where the record says it rests.
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 1,
                                 &mb,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
            const VkImageLayout rest = record->layout;
            record_layout_transition(cmd, *record, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.imageSubresource.aspectMask = single_aspect(*record);
            copy.imageSubresource.mipLevel = region.mip_level;
            copy.imageSubresource.baseArrayLayer = region.layer;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {
                static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), static_cast<int32_t>(region.z)};
            copy.imageExtent = {region.width, region.height, region.depth};
            vkCmdCopyImageToBuffer(cmd, record->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            record_layout_transition(cmd, *record, rest);
            if (!vk_check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (readback)"))
            {
                break;
            }
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            if (!m_device.check_queue_result(vkQueueSubmit(m_device.graphics_queue(), 1, &si, fence),
                                             "vkQueueSubmit (readback)"))
            {
                break;
            }
            if (!m_device.check_queue_result(vkWaitForFences(m_device.handle(), 1, &fence, VK_TRUE, UINT64_MAX),
                                             "vkWaitForFences (readback)"))
            {
                break;
            }
            std::memcpy(out, info.pMappedData, required);
            ok = true;
        } while (false);

        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device.handle(), fence, nullptr);
        }
        if (cmd != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(m_device.handle(), m_transfer.command_pool(), 1, &cmd);
        }
        vmaDestroyBuffer(m_device.allocator(), readback, readback_allocation);
        return ok;
    }

    void vk_device::generate_mipmaps(texture handle)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || record->mip_levels <= 1)
        {
            // Single-level textures (including any whose format could not
            // back a linear blit at create time) have nothing to derive.
            return;
        }
        if (!record->blit_capable)
        {
            LOG_WRN("vk_device::generate_mipmaps: the format cannot be blitted; the chain keeps level 0 only");
            return;
        }

        // The blits join the open transfer batch behind the upload of
        // level 0 that preceded them, and run ahead of the first frame
        // that samples the chain.
        const uint32_t layers = record->array_layers;
        VkCommandBuffer cmd = m_transfer.transfer_command_buffer();
        if (cmd == VK_NULL_HANDLE)
        {
            LOG_ERR("vk_device::generate_mipmaps: no command buffer; the chain keeps level 0 only");
            return;
        }

        // The upload path leaves every level in SHADER_READ_ONLY with only
        // level 0 populated. Move the whole chain to TRANSFER_DST so the
        // canonical blit-down loop starts from a known layout (contents of
        // level 0 are preserved — only an UNDEFINED old layout discards).
        transition_image(cmd,
                         record->image,
                         VK_IMAGE_ASPECT_COLOR_BIT,
                         record->layout,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         record->mip_levels,
                         layers);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = record->image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layers;
        barrier.subresourceRange.levelCount = 1;

        int32_t mip_w = static_cast<int32_t>(record->width);
        int32_t mip_h = static_cast<int32_t>(record->height);

        for (uint32_t level = 1; level < record->mip_levels; ++level)
        {
            // The finer (level - 1) image becomes the blit source.
            barrier.subresourceRange.baseMipLevel = level - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);

            const int32_t dst_w = mip_w > 1 ? mip_w / 2 : 1;
            const int32_t dst_h = mip_h > 1 ? mip_h / 2 : 1;

            VkImageBlit blit{};
            blit.srcOffsets[1] = {mip_w, mip_h, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = level - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layers;
            blit.dstOffsets[1] = {dst_w, dst_h, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = level;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layers;
            vkCmdBlitImage(cmd,
                           record->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           record->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit,
                           VK_FILTER_LINEAR);

            // The source level is done; hand it to the samplers.
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr,
                                 1,
                                 &barrier);

            mip_w = dst_w;
            mip_h = dst_h;
        }

        // The coarsest level was only ever a blit destination; transition
        // it to the sampled layout alongside the rest of the chain.
        barrier.subresourceRange.baseMipLevel = record->mip_levels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        record->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    sampler vk_device::create_sampler(const sampler_descriptor& descriptor)
    {
        vk_sampler record{};
        record.descriptor = descriptor;
        // Anisotropy only when the feature was granted, never past the
        // device's limit; a device without it samples isotropically.
        const float max_anisotropy =
            m_features.sampler_anisotropy ? std::min(descriptor.max_anisotropy, m_limits.max_anisotropy) : 1.0f;
        VkSamplerCreateInfo si = make_sampler_create_info(descriptor, max_anisotropy);
        if (!vk_check(vkCreateSampler(m_device.handle(), &si, nullptr, &record.object), "vkCreateSampler"))
        {
            return {};
        }
        sampler h{};
        h.id = m_samplers.insert(record);
        return h;
    }

    void vk_device::destroy(sampler handle)
    {
        auto* record = m_samplers.lookup(handle.id);
        if (record == nullptr)
        {
            return;
        }
        // Deferred like every other resource: a sampler named by a
        // descriptor set the in-flight command buffer still binds must
        // outlive that submission.
        const VkDevice dev = m_device.handle();
        const VkSampler object = record->object;
        if (object != VK_NULL_HANDLE)
        {
            enqueue_destroy([dev, object] { vkDestroySampler(dev, object, nullptr); });
        }
        record->object = VK_NULL_HANDLE;
        m_samplers.remove(handle.id);
    }

    VkImageView vk_device::storage_image_view(vk_texture& tex, uint32_t level)
    {
        if (level >= tex.storage_views.size() || tex.image == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }
        if (tex.storage_views[level] != VK_NULL_HANDLE)
        {
            return tex.storage_views[level];
        }

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tex.image;
        // The view type matches the GLSL image dimensionality: an
        // @c imageCube cube level (every face through one view), an
        // @c image2DArray level, or an @c image2D / @c image3D slice.
        vi.viewType = sampled_view_type(tex);
        vi.format = tex.vk_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel = level;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount = tex.array_layers;
        if (!vk_check(vkCreateImageView(m_device.handle(), &vi, nullptr, &tex.storage_views[level]),
                      "vkCreateImageView (storage level)"))
        {
            tex.storage_views[level] = VK_NULL_HANDLE;
        }
        return tex.storage_views[level];
    }

    VkImageView vk_device::attachment_image_view(vk_texture& tex, uint32_t mip, uint32_t layer)
    {
        if (tex.image == VK_NULL_HANDLE || mip >= tex.mip_levels || layer >= tex.array_layers)
        {
            return VK_NULL_HANDLE;
        }
        const size_t index = static_cast<size_t>(layer) * tex.mip_levels + mip;
        if (index >= tex.attachment_views.size())
        {
            return VK_NULL_HANDLE;
        }
        if (tex.attachment_views[index] != VK_NULL_HANDLE)
        {
            return tex.attachment_views[index];
        }

        // One level, one layer, as a plain 2D view whatever the image's
        // own shape (a cube face or an array layer renders like any 2D
        // image), carrying every aspect so a depth-stencil attachment
        // clears and stores both planes.
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = tex.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = tex.vk_format;
        vi.subresourceRange.aspectMask = tex.aspect;
        vi.subresourceRange.baseMipLevel = mip;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = layer;
        vi.subresourceRange.layerCount = 1;
        if (!vk_check(vkCreateImageView(m_device.handle(), &vi, nullptr, &tex.attachment_views[index]),
                      "vkCreateImageView (attachment)"))
        {
            tex.attachment_views[index] = VK_NULL_HANDLE;
        }
        return tex.attachment_views[index];
    }

    void vk_device::record_layout_transition(VkCommandBuffer cmd, vk_texture& tex, VkImageLayout new_layout)
    {
        if (cmd == VK_NULL_HANDLE || tex.image == VK_NULL_HANDLE || tex.layout == new_layout)
        {
            return;
        }
        transition_image(cmd, tex.image, tex.aspect, tex.layout, new_layout, tex.mip_levels, tex.array_layers);
        tex.layout = new_layout;
    }

    bool vk_device::transition_storage_image(VkCommandBuffer cmd, texture handle, bool to_general)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE)
        {
            return false;
        }
        const VkImageLayout target = to_general ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (record->layout == target)
        {
            return false;
        }
        transition_image(cmd,
                         record->image,
                         VK_IMAGE_ASPECT_COLOR_BIT,
                         record->layout,
                         target,
                         record->mip_levels,
                         record->array_layers);
        record->layout = target;
        return true;
    }
} // namespace rendering_engine::gpu::backend::vulkan
