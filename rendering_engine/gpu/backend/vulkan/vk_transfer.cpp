// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_transfer.cpp
 * @brief @c vk_transfer: the transfer batches and the staging ring.
 */

#include <rendering_engine/gpu/backend/vulkan/vk_transfer.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>

#include <core/log.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_check.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_logical_device.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_physical_device.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    vk_transfer::vk_transfer(const vk_physical_device& physical_device, vk_logical_device& device)
        : m_physical_device{physical_device}, m_device{device}
    {
    }

    void vk_transfer::create_command_pool()
    {
        // Batches reset their own buffer when a slot is reused (the
        // others may be in flight), so the pool allows it. Transient:
        // every buffer is recorded once and reset.
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = m_physical_device.graphics_queue_family();
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (!vk_check(vkCreateCommandPool(m_device.handle(), &info, nullptr, &m_transfer_command_pool),
                      "vkCreateCommandPool (transfer)"))
        {
            m_transfer_command_pool = VK_NULL_HANDLE;
            throw std::runtime_error{"vkCreateCommandPool failed"};
        }
    }

    void vk_transfer::destroy_command_pool()
    {
        // Under vkDeviceWaitIdle (quit), after discard_transfer_batches:
        // no batch is executing and none holds staging memory, so the
        // fences can go and destroying the pool frees their command
        // buffers.
        discard_transfer_batches();
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.fence != VK_NULL_HANDLE)
            {
                vkDestroyFence(m_device.handle(), batch.fence, nullptr);
            }
        }
        m_transfer_batches.clear();
        if (m_transfer_command_pool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_device.handle(), m_transfer_command_pool, nullptr);
            m_transfer_command_pool = VK_NULL_HANDLE;
        }
    }

    bool vk_transfer::create_staging_ring()
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = k_staging_ring_bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VmaAllocationCreateInfo ai = host_mapped_allocation(/*prefer_host=*/true);
        VmaAllocationInfo info{};
        if (!vk_check(vmaCreateBuffer(m_device.allocator(), &bi, &ai, &m_staging_buffer, &m_staging_allocation, &info),
                      "vmaCreateBuffer (staging ring)"))
        {
            m_staging_buffer = VK_NULL_HANDLE;
            m_staging_allocation = VK_NULL_HANDLE;
            return false;
        }
        if (info.pMappedData == nullptr)
        {
            LOG_ERR("vk_transfer::create_staging_ring: the staging ring allocation is not mapped");
            destroy_staging_ring();
            return false;
        }
        m_staging_mapped = static_cast<uint8_t*>(info.pMappedData);
        m_staging_ring = staging_ring{k_staging_ring_bytes};
        LOG_INF("Vulkan staging ring: %llu MiB, %llu-byte copy alignment",
                static_cast<unsigned long long>(k_staging_ring_bytes / (1024ull * 1024ull)),
                static_cast<unsigned long long>(m_physical_device.copy_offset_alignment()));
        return true;
    }

    void vk_transfer::destroy_staging_ring()
    {
        if (m_staging_buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_device.allocator(), m_staging_buffer, m_staging_allocation);
        }
        m_staging_buffer = VK_NULL_HANDLE;
        m_staging_allocation = VK_NULL_HANDLE;
        m_staging_mapped = nullptr;
        m_staging_ring = staging_ring{};
    }

    vk_transfer::transfer_batch* vk_transfer::open_transfer_batch()
    {
        if (m_device.device_lost() || m_transfer_command_pool == VK_NULL_HANDLE)
        {
            return nullptr;
        }
        if (m_open_transfer_batch != k_no_batch)
        {
            return &m_transfer_batches[m_open_transfer_batch];
        }

        // An idle slot, or a new one. Slots are only ever appended, so
        // an index stays valid; a pointer does not survive the
        // push_back below, which is why callers re-fetch after anything
        // that may open a batch.
        size_t index = k_no_batch;
        for (size_t i = 0; i < m_transfer_batches.size(); ++i)
        {
            if (m_transfer_batches[i].state == transfer_batch::batch_state::idle)
            {
                index = i;
                break;
            }
        }
        if (index == k_no_batch)
        {
            transfer_batch batch{};
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = m_transfer_command_pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            if (!vk_check(vkAllocateCommandBuffers(m_device.handle(), &ai, &batch.cmd),
                          "vkAllocateCommandBuffers (transfer)"))
            {
                return nullptr;
            }
            VkFenceCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (!vk_check(vkCreateFence(m_device.handle(), &fi, nullptr, &batch.fence), "vkCreateFence (transfer)"))
            {
                vkFreeCommandBuffers(m_device.handle(), m_transfer_command_pool, 1, &batch.cmd);
                return nullptr;
            }
            m_transfer_batches.push_back(batch);
            index = m_transfer_batches.size() - 1;
        }

        transfer_batch& batch = m_transfer_batches[index];
        // An idle slot's buffer has completed (its batch was retired)
        // or was never submitted; either way it can be reset and
        // begun again.
        if (!vk_check(vkResetCommandBuffer(batch.cmd, 0), "vkResetCommandBuffer (transfer)"))
        {
            return nullptr;
        }
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!vk_check(vkBeginCommandBuffer(batch.cmd, &bi), "vkBeginCommandBuffer (transfer)"))
        {
            return nullptr;
        }
        // Whatever the queue is still executing when this batch reaches
        // it — the previous frame, when a ring-full flush submits
        // between frames — may read or write the buffers and images the
        // batch copies into. One barrier at the top orders every
        // transfer behind that work and makes its writes available; the
        // per-image layout transitions add the image-specific
        // dependencies on top of it.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(batch.cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);
        batch.id = m_next_transfer_batch_id++;
        batch.state = transfer_batch::batch_state::recording;
        batch.recorded = false;
        batch.submitted = false;
        batch.written_buffers.clear();
        m_open_transfer_batch = index;
        return &batch;
    }

    VkCommandBuffer vk_transfer::transfer_command_buffer()
    {
        transfer_batch* batch = open_transfer_batch();
        if (batch == nullptr)
        {
            return VK_NULL_HANDLE;
        }
        batch->recorded = true;
        return batch->cmd;
    }

    bool vk_transfer::stage_upload(const void* data, size_t size, staged_upload& out)
    {
        out = {};
        if (data == nullptr || size == 0)
        {
            LOG_ERR("vk_transfer::stage_upload: no source data (%zu bytes)", size);
            return false;
        }
        if (m_device.device_lost() || m_staging_buffer == VK_NULL_HANDLE)
        {
            return false;
        }

        // An upload of more than half the ring takes a dedicated buffer:
        // it would otherwise wait for nearly every earlier upload to
        // complete before it could even be staged, for one resource.
        if (size <= m_staging_ring.capacity() / 2)
        {
            for (;;)
            {
                transfer_batch* batch = open_transfer_batch();
                if (batch == nullptr)
                {
                    return false;
                }
                if (const std::optional<uint64_t> offset =
                        m_staging_ring.allocate(size, m_physical_device.copy_offset_alignment()))
                {
                    std::memcpy(m_staging_mapped + *offset, data, size);
                    batch->recorded = true;
                    out.buffer = m_staging_buffer;
                    out.offset = *offset;
                    out.cmd = batch->cmd;
                    return true;
                }
                // The ring is full of bytes that submitted batches (or
                // the open one) still own. Submit what is open so it
                // can complete, then block on the oldest batch — the
                // one wait on the upload path, for one batch rather
                // than the whole queue — and try again with its bytes
                // reclaimed. Each round retires at least one batch, and
                // an empty ring fits anything up to half its capacity,
                // so the loop ends.
                LOG_DBG("vk_transfer::stage_upload: staging ring full (%llu of %llu bytes in use, %zu batches "
                        "submitted); waiting for the oldest batch",
                        static_cast<unsigned long long>(m_staging_ring.used()),
                        static_cast<unsigned long long>(m_staging_ring.capacity()),
                        m_staging_ring.sealed_count());
                flush_transfer_batch();
                if (!wait_oldest_transfer_batch())
                {
                    // Nothing was in flight (or the wait failed): the
                    // ring cannot be freed any further, so stage this
                    // one aside.
                    break;
                }
            }
        }

        transfer_batch* batch = open_transfer_batch();
        if (batch == nullptr)
        {
            return false;
        }
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VmaAllocationCreateInfo ai = host_mapped_allocation(/*prefer_host=*/true);
        VmaAllocationInfo info{};
        transfer_batch::dedicated_staging staging{};
        if (!vk_check(vmaCreateBuffer(m_device.allocator(), &bi, &ai, &staging.buffer, &staging.allocation, &info),
                      "vmaCreateBuffer (dedicated staging)"))
        {
            return false;
        }
        if (info.pMappedData == nullptr)
        {
            LOG_ERR("vk_transfer::stage_upload: the dedicated staging allocation is not mapped");
            vmaDestroyBuffer(m_device.allocator(), staging.buffer, staging.allocation);
            return false;
        }
        std::memcpy(info.pMappedData, data, size);
        // Released with the batch, once its fence proves the copy done.
        batch->dedicated.push_back(staging);
        batch->recorded = true;
        LOG_DBG("vk_transfer::stage_upload: %zu bytes staged in a dedicated buffer for transfer batch %llu",
                size,
                static_cast<unsigned long long>(batch->id));
        out.buffer = staging.buffer;
        out.offset = 0;
        out.cmd = batch->cmd;
        return true;
    }

    void vk_transfer::record_buffer_copy(const staged_upload& source,
                                         VkBuffer dst,
                                         VkDeviceSize dst_offset,
                                         VkDeviceSize size)
    {
        if (source.cmd == VK_NULL_HANDLE)
        {
            return;
        }
        if (m_open_transfer_batch == k_no_batch || m_transfer_batches[m_open_transfer_batch].cmd != source.cmd)
        {
            // stage_upload's batch is always the open one (nothing
            // flushes in between); a staged upload from anywhere else
            // would copy into a batch that is gone.
            LOG_ERR("vk_transfer::record_buffer_copy: the staged upload's transfer batch is no longer open; %llu bytes "
                    "not copied",
                    static_cast<unsigned long long>(size));
            return;
        }
        transfer_batch& batch = m_transfer_batches[m_open_transfer_batch];
        // Two copies into one buffer within a batch are a
        // write-after-write hazard between transfer commands; the second
        // waits for the first. A buffer written once needs nothing.
        if (std::find(batch.written_buffers.begin(), batch.written_buffers.end(), dst) != batch.written_buffers.end())
        {
            VkMemoryBarrier mb{};
            mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(source.cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 1,
                                 &mb,
                                 0,
                                 nullptr,
                                 0,
                                 nullptr);
        }
        else
        {
            batch.written_buffers.push_back(dst);
        }
        VkBufferCopy region{};
        region.srcOffset = source.offset;
        region.dstOffset = dst_offset;
        region.size = size;
        vkCmdCopyBuffer(source.cmd, source.buffer, dst, 1, &region);
    }

    bool vk_transfer::flush_transfer_batch()
    {
        if (m_open_transfer_batch == k_no_batch)
        {
            return true;
        }
        transfer_batch& batch = m_transfer_batches[m_open_transfer_batch];
        m_open_transfer_batch = k_no_batch;
        batch.written_buffers.clear();

        if (!batch.recorded)
        {
            // Begun for nothing: back to the pool, no submission. The
            // next open resets the buffer, so the begin needs no end.
            batch.state = transfer_batch::batch_state::idle;
            return true;
        }

        // Every copy above is a transfer write; make all of them
        // available to whatever follows in queue order — vertex and
        // index fetches, uniform and storage reads, indirect reads,
        // later copies — in one barrier per batch. Image uploads also
        // carry their own layout transitions with the sampling stages
        // as destination.
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(batch.cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             1,
                             &mb,
                             0,
                             nullptr,
                             0,
                             nullptr);

        // From here on the batch is in flight whether or not the
        // submission succeeds: its ring bytes were sealed to it, and
        // retiring it in order (without a fence wait when nothing was
        // submitted) is what keeps the ring's FIFO consistent.
        batch.state = transfer_batch::batch_state::in_flight;
        m_staging_ring.seal(batch.id);
        const bool ended = vk_check(vkEndCommandBuffer(batch.cmd), "vkEndCommandBuffer (transfer)");
        if (!ended || m_device.device_lost())
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        if (!vk_check(vkResetFences(m_device.handle(), 1, &batch.fence), "vkResetFences (transfer)"))
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &batch.cmd;
        if (!m_device.check_queue_result(vkQueueSubmit(m_device.graphics_queue(), 1, &si, batch.fence),
                                         "vkQueueSubmit (transfer)"))
        {
            LOG_ERR("vk_device: transfer batch %llu dropped; the uploads it carried never reach the GPU",
                    static_cast<unsigned long long>(batch.id));
            return false;
        }
        batch.submitted = true;
        return true;
    }

    vk_transfer::transfer_batch* vk_transfer::oldest_transfer_batch()
    {
        transfer_batch* oldest = nullptr;
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state == transfer_batch::batch_state::in_flight && (oldest == nullptr || batch.id < oldest->id))
            {
                oldest = &batch;
            }
        }
        return oldest;
    }

    void vk_transfer::retire_transfer_batch(transfer_batch& batch)
    {
        m_staging_ring.retire(batch.id);
        for (const transfer_batch::dedicated_staging& staging : batch.dedicated)
        {
            vmaDestroyBuffer(m_device.allocator(), staging.buffer, staging.allocation);
        }
        batch.dedicated.clear();
        batch.submitted = false;
        batch.recorded = false;
        batch.state = transfer_batch::batch_state::idle;
    }

    void vk_transfer::retire_transfer_batches()
    {
        // Oldest first: a fence that has signaled proves every earlier
        // submission on the queue complete, and one that has not stops
        // the walk, since nothing after it can be done either.
        for (transfer_batch* batch = oldest_transfer_batch(); batch != nullptr; batch = oldest_transfer_batch())
        {
            if (batch->submitted)
            {
                const VkResult status = vkGetFenceStatus(m_device.handle(), batch->fence);
                if (status == VK_NOT_READY)
                {
                    return;
                }
                if (status != VK_SUCCESS)
                {
                    m_device.check_queue_result(status, "vkGetFenceStatus (transfer)");
                    return;
                }
            }
            retire_transfer_batch(*batch);
        }
    }

    bool vk_transfer::transfer_batch_live_up_to(uint64_t batch_id) const
    {
        for (const transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state != transfer_batch::batch_state::idle && batch.id <= batch_id)
            {
                return true;
            }
        }
        return false;
    }

    void vk_transfer::discard_transfer_batches()
    {
        for (transfer_batch& batch : m_transfer_batches)
        {
            if (batch.state != transfer_batch::batch_state::idle)
            {
                retire_transfer_batch(batch);
            }
        }
        m_open_transfer_batch = k_no_batch;
    }

    bool vk_transfer::wait_oldest_transfer_batch()
    {
        transfer_batch* batch = oldest_transfer_batch();
        if (batch == nullptr)
        {
            return false;
        }
        if (batch->submitted &&
            !m_device.check_queue_result(vkWaitForFences(m_device.handle(), 1, &batch->fence, VK_TRUE, UINT64_MAX),
                                         "vkWaitForFences (transfer)"))
        {
            return false;
        }
        retire_transfer_batch(*batch);
        return true;
    }

    uint64_t vk_transfer::newest_transfer_batch_id() const noexcept
    {
        return m_next_transfer_batch_id - 1;
    }

    VkCommandPool vk_transfer::command_pool() const noexcept
    {
        return m_transfer_command_pool;
    }

    void vk_transfer::reset()
    {
        m_next_transfer_batch_id = 1;
    }
} // namespace rendering_engine::gpu::backend::vulkan
