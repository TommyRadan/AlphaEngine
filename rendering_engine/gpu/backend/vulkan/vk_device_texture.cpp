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

/**
 * @file vk_device_texture.cpp
 * @brief @c vk_device member functions that manage @c VkImage,
 *        @c VkImageView and @c VkSampler objects.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

#include <core/log.hpp>
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

        // Full mip-chain length for a texture of the given footprint:
        // floor(log2(max(w, h))) + 1, matching glGenerateMipmap's chain.
        uint32_t full_mip_chain(uint32_t width, uint32_t height)
        {
            const uint32_t largest = std::max(width, height);
            uint32_t levels = 1;
            uint32_t extent = largest;
            while (extent > 1)
            {
                extent >>= 1;
                ++levels;
            }
            return levels;
        }

        // vkCmdBlitImage with VK_FILTER_LINEAR — the down-sampling
        // primitive @ref vk_device::generate_mipmaps relies on — is only
        // valid when the format advertises blit-src, blit-dst and linear
        // sample filtering with optimal tiling. The engine's mipmapped
        // textures are rgba8_unorm / rgba16_float, which support this on
        // every desktop GPU, but a format that doesn't gets a single
        // level rather than an undefined chain.
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
    } // namespace

    texture vk_device::create_texture(const texture_descriptor& descriptor)
    {
        vk_texture record{};
        record.format = descriptor.format;
        record.vk_format = to_vk_format(descriptor.format);
        record.width = descriptor.width;
        record.height = descriptor.height;
        record.depth = (descriptor.dimension == texture_dimension::d3) ? descriptor.depth : 1u;
        record.mipmaps = descriptor.mipmaps;
        record.is_cube = descriptor.dimension == texture_dimension::cube;
        record.is_3d = descriptor.dimension == texture_dimension::d3;
        record.is_depth = is_depth_format(descriptor.format);
        record.array_layers = record.is_cube ? 6u : 1u;
        record.mip_levels = 1;

        // A mipmapped colour texture allocates its whole chain up front so
        // every level is addressable; @ref generate_mipmaps fills levels
        // 1..n via vkCmdBlitImage. Depth targets and 3D textures keep a
        // single level (the engine never requests mips for either, and a
        // 3D blit would also have to halve depth). Formats without linear
        // blit support fall back to one level rather than leaving the
        // chain undefined.
        if (descriptor.mipmaps && !record.is_depth && !record.is_3d &&
            format_supports_linear_blit(m_physical_device, record.vk_format))
        {
            record.mip_levels = full_mip_chain(record.width, record.height);
        }
        record.storage_views.assign(record.mip_levels, VK_NULL_HANDLE);

        // Storage usage is opt-in (the @c storage descriptor flag) so the
        // common sampled texture keeps its framebuffer-compression-
        // friendly usage set; only resources bound as storage images
        // (the IBL convolution outputs) pay for the extra bit.
        record.storage = descriptor.storage && !record.is_depth &&
                         format_supports_storage_image(m_physical_device, record.vk_format);

        VkImageUsageFlags usage =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (record.is_depth)
        {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        else
        {
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (record.storage)
        {
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = record.is_3d ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        ii.format = record.vk_format;
        ii.extent = {record.width, record.height, record.depth};
        ii.mipLevels = record.mip_levels;
        ii.arrayLayers = record.array_layers;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ii.flags = record.is_cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        if (vkCreateImage(m_device, &ii, nullptr, &record.image) != VK_SUCCESS)
        {
            return {};
        }

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(m_device, record.image, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &ai, nullptr, &record.memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_device, record.image, nullptr);
            return {};
        }
        vkBindImageMemory(m_device, record.image, record.memory, 0);

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = record.image;
        vi.viewType =
            record.is_cube ? VK_IMAGE_VIEW_TYPE_CUBE : (record.is_3d ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D);
        vi.format = record.vk_format;
        vi.subresourceRange.aspectMask = aspect_for_format(descriptor.format);
        vi.subresourceRange.levelCount = record.mip_levels;
        vi.subresourceRange.layerCount = record.array_layers;
        if (vkCreateImageView(m_device, &vi, nullptr, &record.view) != VK_SUCCESS)
        {
            vkDestroyImage(m_device, record.image, nullptr);
            vkFreeMemory(m_device, record.memory, nullptr);
            return {};
        }

        VkSamplerCreateInfo si = make_sampler_create_info(descriptor.min_filter,
                                                          descriptor.mag_filter,
                                                          descriptor.mipmap_filter,
                                                          descriptor.address_u,
                                                          descriptor.address_v,
                                                          descriptor.address_w);
        vkCreateSampler(m_device, &si, nullptr, &record.default_sampler);

        record.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkCommandBuffer cmd = begin_one_shot();
        const VkImageLayout target = record.is_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transition_image(cmd,
                         record.image,
                         aspect_for_format(descriptor.format),
                         VK_IMAGE_LAYOUT_UNDEFINED,
                         target,
                         record.mip_levels,
                         record.array_layers);
        end_one_shot(cmd);
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
        VkDevice dev = m_device;
        VkSampler sampler = record->default_sampler;
        VkImageView view = record->view;
        std::vector<VkImageView> storage_views = std::move(record->storage_views);
        VkImage image = record->external ? VK_NULL_HANDLE : record->image;
        VkDeviceMemory memory = record->memory;
        // Deferred for the same reason as destroy(buffer): a texture
        // sampled by an in-flight command buffer must outlive the
        // submission that referenced it.
        enqueue_destroy(
            [dev, sampler, view, storage_views = std::move(storage_views), image, memory]
            {
                if (sampler != VK_NULL_HANDLE)
                {
                    vkDestroySampler(dev, sampler, nullptr);
                }
                if (view != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(dev, view, nullptr);
                }
                for (VkImageView storage_view : storage_views)
                {
                    if (storage_view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(dev, storage_view, nullptr);
                    }
                }
                if (image != VK_NULL_HANDLE)
                {
                    vkDestroyImage(dev, image, nullptr);
                }
                if (memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(dev, memory, nullptr);
                }
            });
        record->default_sampler = VK_NULL_HANDLE;
        record->view = VK_NULL_HANDLE;
        record->image = VK_NULL_HANDLE;
        record->memory = VK_NULL_HANDLE;
        m_textures.remove(handle.id);
    }

    namespace
    {
        void upload_region(vk_device& device,
                           vk_texture& record,
                           uint32_t base_layer,
                           const VkExtent3D& extent,
                           const void* data,
                           size_t size)
        {
            const VkBuffer staging = device.stage_upload(data, size);
            if (staging == VK_NULL_HANDLE)
            {
                return;
            }

            VkCommandBuffer cmd = device.begin_one_shot();
            transition_image(cmd,
                             record.image,
                             VK_IMAGE_ASPECT_COLOR_BIT,
                             record.layout,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             record.mip_levels,
                             record.array_layers);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageSubresource.baseArrayLayer = base_layer;
            region.imageExtent = extent;
            vkCmdCopyBufferToImage(cmd, staging, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            transition_image(cmd,
                             record.image,
                             VK_IMAGE_ASPECT_COLOR_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             record.mip_levels,
                             record.array_layers);
            device.end_one_shot(cmd);
            record.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    } // namespace

    void vk_device::write_texture(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || record->is_cube || record->is_3d)
        {
            return;
        }
        upload_region(*this, *record, 0, {record->width, record->height, 1}, data, size);
    }

    void vk_device::write_texture_3d(texture handle, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || !record->is_3d)
        {
            return;
        }
        upload_region(*this, *record, 0, {record->width, record->height, record->depth}, data, size);
    }

    void vk_device::write_cube_face(texture handle, cube_face face, const void* data, size_t size)
    {
        auto* record = m_textures.lookup(handle.id);
        if (record == nullptr || record->image == VK_NULL_HANDLE || !record->is_cube)
        {
            return;
        }
        upload_region(*this, *record, static_cast<uint32_t>(face), {record->width, record->height, 1}, data, size);
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

        const uint32_t layers = record->array_layers;
        VkCommandBuffer cmd = begin_one_shot();

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

        end_one_shot(cmd);
        record->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    sampler vk_device::create_sampler(const sampler_descriptor& descriptor)
    {
        vk_sampler record{};
        record.descriptor = descriptor;
        VkSamplerCreateInfo si = make_sampler_create_info(descriptor.min_filter,
                                                          descriptor.mag_filter,
                                                          descriptor.mipmap,
                                                          descriptor.address_u,
                                                          descriptor.address_v,
                                                          descriptor.address_w);
        if (vkCreateSampler(m_device, &si, nullptr, &record.object) != VK_SUCCESS)
        {
            return {};
        }
        sampler h{};
        h.id = m_samplers.insert(record);
        return h;
    }

    void vk_device::destroy(sampler handle)
    {
        if (auto* record = m_samplers.lookup(handle.id))
        {
            if (record->object != VK_NULL_HANDLE)
            {
                vkDestroySampler(m_device, record->object, nullptr);
                record->object = VK_NULL_HANDLE;
            }
            m_samplers.remove(handle.id);
        }
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
        // @c imageCube cube level (every face through one view) or an
        // @c image2D / @c image3D slice.
        vi.viewType =
            tex.is_cube ? VK_IMAGE_VIEW_TYPE_CUBE : (tex.is_3d ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D);
        vi.format = tex.vk_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel = level;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount = tex.array_layers;
        if (vkCreateImageView(m_device, &vi, nullptr, &tex.storage_views[level]) != VK_SUCCESS)
        {
            tex.storage_views[level] = VK_NULL_HANDLE;
        }
        return tex.storage_views[level];
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
