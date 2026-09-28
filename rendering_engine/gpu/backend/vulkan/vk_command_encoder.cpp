// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_command_encoder.cpp
 * @brief @c vk_command_encoder: a primary command buffer, the passes
 *        it begins, and the copies, barriers, timestamp queries and
 *        debug groups recorded outside a pass.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_command_encoder.hpp>

#include <algorithm>
#include <array>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_translate.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        // True when @p region lies inside @p record and @p size bytes
        // from @p offset stay inside @p buffer; logs the first problem
        // otherwise.
        bool copy_region_fits(const char* where,
                              const vk_texture& record,
                              const texture_copy_region& region,
                              const vk_buffer& buffer,
                              size_t offset)
        {
            if (record.samples > 1)
            {
                LOG_WRN("%s: a multisampled texture cannot be copied", where);
                return false;
            }
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
                LOG_WRN("%s: region does not fit level %u", where, region.mip_level);
                return false;
            }
            const size_t bytes = texture_region_bytes(record.format, region);
            if (offset > buffer.size || bytes > buffer.size - offset)
            {
                LOG_WRN("%s: %zu bytes at offset %zu exceed the %zu-byte buffer", where, bytes, offset, buffer.size);
                return false;
            }
            return true;
        }

        VkBufferImageCopy
        make_buffer_image_copy(const vk_texture& record, const texture_copy_region& region, size_t offset)
        {
            VkBufferImageCopy copy{};
            copy.bufferOffset = offset;
            copy.imageSubresource.aspectMask = (record.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u
                                                   ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                   : VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = region.mip_level;
            copy.imageSubresource.baseArrayLayer = region.layer;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {
                static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), static_cast<int32_t>(region.z)};
            copy.imageExtent = {region.width, region.height, region.depth};
            return copy;
        }
    } // namespace

    // -- vk_command_encoder -----------------------------------------

    vk_command_encoder::vk_command_encoder(vk_device& device) : m_device{device}
    {
        // The buffer comes from the device's frame command pool, which
        // hands it out reset and takes it back with the pool reset at
        // the next begin_frame: nothing is allocated or freed per frame
        // once the pool has grown to the frame's encoder count. A null
        // buffer (lost device, failed allocation — logged there) leaves
        // the encoder inert: every pass it begins records nothing.
        m_cmd = device.acquire_frame_command_buffer();
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const VkResult begin_result = vkBeginCommandBuffer(m_cmd, &bi);
        if (begin_result != VK_SUCCESS)
        {
            LOG_ERR("vkBeginCommandBuffer failed: %s", vk_result_to_string(begin_result));
            m_cmd = VK_NULL_HANDLE;
            return;
        }
        m_began = true;
    }

    vk_command_encoder::~vk_command_encoder()
    {
        // An encoder dropped without a submit leaves its buffer to the
        // pool: the reset at the next begin_frame reclaims it along with
        // the frame's submitted one.
        m_cmd = VK_NULL_HANDLE;
    }

    std::unique_ptr<render_pass_encoder> vk_command_encoder::begin_render_pass(const render_pass_descriptor& descriptor)
    {
        return std::make_unique<vk_render_pass_encoder>(m_device, m_cmd, descriptor);
    }

    std::unique_ptr<compute_pass_encoder> vk_command_encoder::begin_compute_pass()
    {
        return std::make_unique<vk_compute_pass_encoder>(m_device, m_cmd);
    }

    void
    vk_command_encoder::copy_buffer_to_buffer(buffer src, size_t src_offset, buffer dst, size_t dst_offset, size_t size)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_buf = m_device.lookup_buffer(src);
        auto* dst_buf = m_device.lookup_buffer(dst);
        if (src_buf == nullptr || dst_buf == nullptr || src_buf->object == VK_NULL_HANDLE ||
            dst_buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        // Both sides go through this frame slot's copy. A GPU write
        // into a multi-buffered destination reaches that copy alone:
        // the device carries host writes across the copies, not what
        // the GPU wrote (see vk_buffer::region_count).
        m_device.ensure_host_region_current(*src_buf);
        VkBufferCopy region{};
        region.srcOffset = m_device.host_region_offset(*src_buf) + src_offset;
        region.dstOffset = m_device.host_region_offset(*dst_buf) + dst_offset;
        region.size = size;
        vkCmdCopyBuffer(m_cmd, src_buf->object, dst_buf->object, 1, &region);
    }

    void vk_command_encoder::clear_buffer(buffer buffer_handle, size_t offset, size_t size, uint32_t value)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* buf = m_device.lookup_buffer(buffer_handle);
        if (buf == nullptr || buf->object == VK_NULL_HANDLE)
        {
            return;
        }
        // See copy_buffer_to_buffer on a multi-buffered destination.
        vkCmdFillBuffer(m_cmd, buf->object, m_device.host_region_offset(*buf) + offset, size, value);
    }

    void vk_command_encoder::barrier(pipeline_stage src_stage,
                                     pipeline_stage dst_stage,
                                     access_flag src_access,
                                     access_flag dst_access)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        // Translate the caller's stage/access intent into precise Vulkan masks
        // instead of a blanket ALL_COMMANDS full barrier. An empty stage mask
        // is illegal, so an unspecified source waits from the top of the pipe
        // and an unspecified destination blocks at the bottom.
        VkPipelineStageFlags src = to_vk_pipeline_stage(src_stage);
        VkPipelineStageFlags dst = to_vk_pipeline_stage(dst_stage);
        if (src == 0u)
        {
            src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        }
        if (dst == 0u)
        {
            dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        }
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = to_vk_access(src_access);
        mb.dstAccessMask = to_vk_access(dst_access);
        vkCmdPipelineBarrier(m_cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    void vk_command_encoder::copy_buffer_to_texture(buffer src,
                                                    size_t src_offset,
                                                    texture dst,
                                                    const texture_copy_region& region)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_buf = m_device.lookup_buffer(src);
        auto* dst_tex = m_device.lookup_texture(dst);
        if (src_buf == nullptr || src_buf->object == VK_NULL_HANDLE || dst_tex == nullptr ||
            dst_tex->image == VK_NULL_HANDLE)
        {
            LOG_WRN("copy_buffer_to_texture: invalid src buffer / dst texture");
            return;
        }
        if ((dst_tex->usage & texture_usage_copy_dst) == 0u)
        {
            LOG_WRN("copy_buffer_to_texture: the texture was created without texture_usage_copy_dst");
            return;
        }
        if (!copy_region_fits("copy_buffer_to_texture", *dst_tex, region, *src_buf, src_offset))
        {
            return;
        }
        // The buffer's writes (a host write, or an earlier copy in this
        // stream) must be visible to the transfer; the image moves to
        // the transfer layout for the copy and back to its resting
        // layout, in stream order with the passes around it.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(m_cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
        const VkImageLayout rest = dst_tex->layout;
        m_device.record_layout_transition(m_cmd, *dst_tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        m_device.ensure_host_region_current(*src_buf);
        const VkBufferImageCopy copy =
            make_buffer_image_copy(*dst_tex, region, m_device.host_region_offset(*src_buf) + src_offset);
        vkCmdCopyBufferToImage(m_cmd, src_buf->object, dst_tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        m_device.record_layout_transition(
            m_cmd, *dst_tex, rest == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : rest);
    }

    void vk_command_encoder::copy_texture_to_buffer(texture src,
                                                    const texture_copy_region& region,
                                                    buffer dst,
                                                    size_t dst_offset)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* src_tex = m_device.lookup_texture(src);
        auto* dst_buf = m_device.lookup_buffer(dst);
        if (src_tex == nullptr || src_tex->image == VK_NULL_HANDLE || dst_buf == nullptr ||
            dst_buf->object == VK_NULL_HANDLE)
        {
            LOG_WRN("copy_texture_to_buffer: invalid src texture / dst buffer");
            return;
        }
        if ((src_tex->usage & texture_usage_copy_src) == 0u)
        {
            LOG_WRN("copy_texture_to_buffer: the texture was created without texture_usage_copy_src");
            return;
        }
        if (!copy_region_fits("copy_texture_to_buffer", *src_tex, region, *dst_buf, dst_offset))
        {
            return;
        }
        const VkImageLayout rest = src_tex->layout;
        m_device.record_layout_transition(m_cmd, *src_tex, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        // See copy_buffer_to_buffer on a multi-buffered destination.
        const VkBufferImageCopy copy =
            make_buffer_image_copy(*src_tex, region, m_device.host_region_offset(*dst_buf) + dst_offset);
        vkCmdCopyImageToBuffer(m_cmd, src_tex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_buf->object, 1, &copy);
        m_device.record_layout_transition(
            m_cmd, *src_tex, rest == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : rest);
        // Make the transferred bytes visible to a host read (a mapped
        // readback buffer) and to whatever consumes the buffer next.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(m_cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
    }

    void vk_command_encoder::push_debug_group(const char* name)
    {
        const PFN_vkCmdBeginDebugUtilsLabelEXT begin_label = m_device.cmd_begin_debug_label();
        if (m_cmd == VK_NULL_HANDLE || begin_label == nullptr)
        {
            return;
        }
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name != nullptr ? name : "";
        begin_label(m_cmd, &label);
        ++m_debug_label_depth;
    }

    void vk_command_encoder::pop_debug_group()
    {
        const PFN_vkCmdEndDebugUtilsLabelEXT end_label = m_device.cmd_end_debug_label();
        if (m_cmd == VK_NULL_HANDLE || end_label == nullptr || m_debug_label_depth == 0)
        {
            return;
        }
        end_label(m_cmd);
        --m_debug_label_depth;
    }

    void vk_command_encoder::reset_queries(query_set set, uint32_t first, uint32_t count)
    {
        if (m_cmd == VK_NULL_HANDLE || count == 0)
        {
            return;
        }
        auto* record = m_device.lookup_query_set(set);
        if (record == nullptr || record->pool == VK_NULL_HANDLE)
        {
            return;
        }
        if (first > record->count || count > record->count - first)
        {
            LOG_WRN("reset_queries: %u queries from %u exceed the %u-query set", count, first, record->count);
            return;
        }
        // Must be recorded outside a render pass; the caller resets a
        // frame's queries at its top, before any pass opens.
        vkCmdResetQueryPool(m_cmd, record->pool, first, count);
    }

    void vk_command_encoder::write_timestamp(query_set set, uint32_t index)
    {
        if (m_cmd == VK_NULL_HANDLE)
        {
            return;
        }
        auto* record = m_device.lookup_query_set(set);
        if (record == nullptr || record->pool == VK_NULL_HANDLE)
        {
            return;
        }
        if (index >= record->count)
        {
            LOG_WRN("write_timestamp: query %u of a %u-query set", index, record->count);
            return;
        }
        // Bottom of pipe: the stamp lands once everything recorded
        // before it has executed, which is what a per-pass interval
        // wants.
        vkCmdWriteTimestamp(m_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, record->pool, index);
    }

    VkCommandBuffer vk_command_encoder::release_command_buffer() noexcept
    {
        VkCommandBuffer released = m_cmd;
        m_cmd = VK_NULL_HANDLE;
        m_began = false;
        return released;
    }
} // namespace rendering_engine::gpu::backend::vulkan
