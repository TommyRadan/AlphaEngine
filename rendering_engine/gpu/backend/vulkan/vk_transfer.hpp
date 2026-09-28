// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_transfer.hpp
 * @brief Uploads: the transfer batches and the staging ring their
 *        bytes are copied from.
 *
 * Every upload (buffer initial data, write_buffer to device-local
 * memory, texture uploads, mipmap generation, the initial layout
 * transition of a new image) is recorded into the open transfer
 * batch: one primary command buffer with its own fence. The batch is
 * submitted ahead of the frame's command buffer in vk_device::submit,
 * or earlier when the staging ring runs out of space. It opens with
 * an all-commands -> transfer barrier, so its copies run behind
 * whatever the queue was still executing (the previous frame, for a
 * flush between frames), and ends with a transfer -> all-commands
 * memory barrier, so whatever the frame reads after it in queue order
 * sees the uploads. Nothing here waits the queue idle: the ring space
 * and the dedicated staging buffers of a batch are reclaimed when its
 * fence signals (begin_frame, or a ring-full wait for the oldest
 * batch). The CPU-side image layout each vk_texture records is its
 * layout in batch order; a compute pass moves a storage image to
 * GENERAL inside the frame's command buffer and back before it ends,
 * so no upload may target a texture bound by a compute pass that is
 * still open.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include <rendering_engine/gpu/backend/vulkan/vk_allocator.hpp>
#include <rendering_engine/gpu/backend/vulkan/vk_staging_ring.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    class vk_logical_device;
    class vk_physical_device;

    class vk_transfer
    {
    public:
        vk_transfer(const vk_physical_device& physical_device, vk_logical_device& device);

        // The pool the batches allocate from (it allows per-buffer
        // resets, so an idle slot's buffer is reused without touching
        // the others). Throws when the pool cannot be created.
        void create_command_pool();
        // Under vkDeviceWaitIdle (quit): drop every batch, destroy the
        // batches' fences and the pool, which frees their buffers.
        void destroy_command_pool();
        // The persistently mapped staging ring (k_staging_ring_bytes).
        // Returns false, with the failure logged, when the buffer could
        // not be allocated or mapped; init treats that as fatal.
        bool create_staging_ring();
        void destroy_staging_ring();

        // The open batch's command buffer, begun on first use. Returns
        // VK_NULL_HANDLE (callers skip their upload, which is logged)
        // when no batch can be begun or the device is lost.
        VkCommandBuffer transfer_command_buffer();

        // Source of a staged copy: @p offset bytes into @p buffer hold
        // the caller's data, and @p cmd is the batch to record the copy
        // into. Both are valid until the batch is flushed, which nothing
        // does between stage_upload and the copy that follows it.
        struct staged_upload
        {
            VkBuffer buffer{VK_NULL_HANDLE};
            VkDeviceSize offset{0};
            VkCommandBuffer cmd{VK_NULL_HANDLE};
        };

        // Copy @p size bytes of @p data into staging memory for the open
        // batch: the ring when the upload fits (a full ring flushes the
        // open batch and waits for the oldest submitted one, which is
        // the only wait on the upload path), a dedicated host-visible
        // buffer released with the batch when it does not. The ring
        // offset is aligned for any vkCmdCopyBufferToImage texel block
        // the engine's formats have. Returns false, with the reason
        // logged, when nothing could be staged.
        bool stage_upload(const void* data, size_t size, staged_upload& out);

        // Record the copy of a staged upload into @p dst at
        // @p dst_offset, with the transfer -> transfer barrier a second
        // copy into the same buffer within one batch needs.
        void record_buffer_copy(const staged_upload& source, VkBuffer dst, VkDeviceSize dst_offset, VkDeviceSize size);

        // End and queue the open transfer batch on its fence; a batch
        // that recorded nothing is returned to the pool unsubmitted.
        // Returns false when the submission failed (the uploads it
        // carried are lost, logged as an error) — the batch is still
        // retired in order so the ring stays consistent.
        bool flush_transfer_batch();

        // Reclaim submitted batches in submission order, polling their
        // fences: every batch up to the first one still executing.
        void retire_transfer_batches();
        // Drop every batch without a fence wait, releasing what it
        // holds: only under vkDeviceWaitIdle or once the device is
        // lost, when nothing executes any more (quit).
        void discard_transfer_batches();

        // The id of the newest batch begun so far (0 when none has
        // begun): the last one that can hold a copy into a resource
        // destroyed now.
        uint64_t newest_transfer_batch_id() const noexcept;
        // True while a batch with an id up to @p batch_id is still
        // recording or executing — the gate a deferred destroy waits
        // behind.
        bool transfer_batch_live_up_to(uint64_t batch_id) const;

        // The pool the batches come from, for a one-off command buffer
        // recorded and waited for on the spot (vk_device::read_texture).
        VkCommandPool command_pool() const noexcept;
        // Restart the batch ids (quit, for the next init).
        void reset();

    private:
        // One transfer batch: a command buffer from the transfer pool
        // and the fence its submission signals. A slot cycles
        // idle -> recording -> in_flight -> idle; the ring bytes and
        // dedicated staging buffers it holds are released when it is
        // retired.
        struct transfer_batch
        {
            enum class batch_state
            {
                idle,
                recording,
                in_flight
            };
            struct dedicated_staging
            {
                VkBuffer buffer{VK_NULL_HANDLE};
                VmaAllocation allocation{VK_NULL_HANDLE};
            };

            VkCommandBuffer cmd{VK_NULL_HANDLE};
            VkFence fence{VK_NULL_HANDLE};
            uint64_t id{0};
            batch_state state{batch_state::idle};
            // Something was recorded (or staged) into the open batch,
            // so flush submits it; an untouched batch goes back idle.
            bool recorded{false};
            // The submission reached the queue, so retiring the batch
            // has to wait for its fence. A batch whose submit failed is
            // retired in turn without a wait.
            bool submitted{false};
            std::vector<dedicated_staging> dedicated;
            // Destination buffers already copied into by this batch;
            // a second copy into one of them is preceded by a
            // transfer -> transfer barrier.
            std::vector<VkBuffer> written_buffers;
        };
        // The open batch, begun (or reused from an idle slot) on first
        // use. Null when none can be begun.
        transfer_batch* open_transfer_batch();
        // Release a completed batch: its ring bytes, its dedicated
        // staging buffers, and the slot.
        void retire_transfer_batch(transfer_batch& batch);
        // Block on the oldest submitted batch and retire it. Returns
        // false when there is none or the wait failed.
        bool wait_oldest_transfer_batch();
        // The submitted batch with the lowest id, or null.
        transfer_batch* oldest_transfer_batch();

        const vk_physical_device& m_physical_device;
        vk_logical_device& m_device;

        // Slots are appended as needed and never freed before quit.
        VkCommandPool m_transfer_command_pool{VK_NULL_HANDLE};
        std::vector<transfer_batch> m_transfer_batches;
        // Index into m_transfer_batches of the recording batch, or
        // k_no_batch.
        static constexpr size_t k_no_batch = static_cast<size_t>(-1);
        size_t m_open_transfer_batch{k_no_batch};
        uint64_t m_next_transfer_batch_id{1};

        // The staging ring: one host-visible, host-coherent buffer,
        // mapped for the lifetime of the device, whose bytes are
        // handed out by m_staging_ring. Uploads larger than half of it
        // take a dedicated buffer instead (stage_upload).
        static constexpr VkDeviceSize k_staging_ring_bytes = 32ull * 1024ull * 1024ull;
        VkBuffer m_staging_buffer{VK_NULL_HANDLE};
        VmaAllocation m_staging_allocation{VK_NULL_HANDLE};
        uint8_t* m_staging_mapped{nullptr};
        staging_ring m_staging_ring;
    };
} // namespace rendering_engine::gpu::backend::vulkan
