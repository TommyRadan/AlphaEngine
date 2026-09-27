// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file vk_staging_ring.hpp
 * @brief Offset bookkeeping for the Vulkan backend's persistently
 *        mapped staging ring.
 *
 * The ring is one host-visible buffer the device fills front to back
 * with the bytes of every upload (buffer initial data, device-local
 * write_buffer, texture uploads). Every upload's copy command is
 * recorded into the transfer batch that is open at the time, and the
 * ring space it took stays reserved until that batch's fence has
 * signaled. This class is only the arithmetic — reserve, wrap, seal a
 * batch, retire it — over plain integers: no @c VkBuffer, no fence, so
 * @c vk_device can drive it against the real buffer while the unit
 * tests pin the offset / alignment / wrap / reclaim rules without a
 * GPU.
 *
 * Positions are absolute, monotonically increasing byte counters
 * (@c head is where the next reservation starts, @c tail is the start
 * of the oldest byte still in use); the buffer offset is a position
 * modulo the capacity. A reservation that would straddle the end of
 * the buffer skips to the next lap, and the skipped bytes count as
 * used until the batch that skipped them is retired, so a reservation
 * is always one contiguous range of the buffer.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace rendering_engine::gpu::backend::vulkan
{
    class staging_ring
    {
    public:
        staging_ring() = default;
        explicit staging_ring(uint64_t capacity);

        // Reserve @p size bytes for the open batch at an offset that is
        // a multiple of @p alignment (0 counts as 1). Returns the byte
        // offset into the buffer, or nothing when @p size is 0, exceeds
        // the capacity, or does not fit in the space that sealed
        // batches have not released yet — the caller then flushes and
        // waits for the oldest batch, or stages elsewhere. With a
        // capacity that is a multiple of @p alignment the offset is
        // aligned on every lap; otherwise a wrapped reservation lands
        // at offset 0, which is aligned to anything.
        std::optional<uint64_t> allocate(uint64_t size, uint64_t alignment);

        // Close the open batch: every byte reserved since the previous
        // seal belongs to @p batch_id until retire() releases it.
        // Returns false, recording nothing, when the open batch
        // reserved no bytes. Batch ids must increase from one seal to
        // the next; retire() relies on that order.
        bool seal(uint64_t batch_id);

        // Release the bytes of every sealed batch whose id is at most
        // @p batch_id (a fence that signals for a later batch covers the
        // earlier ones, which were submitted ahead of it on the same
        // queue). Ids that were never sealed — an empty batch — are
        // skipped.
        void retire(uint64_t batch_id);

        uint64_t capacity() const noexcept;
        // Bytes between the oldest byte still in use and the write
        // cursor: everything held by sealed batches plus the open
        // batch, skipped lap tails included. Never exceeds capacity().
        uint64_t used() const noexcept;
        // Bytes reserved by the open (unsealed) batch.
        uint64_t pending() const noexcept;
        // Sealed batches not yet retired.
        size_t sealed_count() const noexcept;
        // The id of the oldest sealed, unretired batch.
        std::optional<uint64_t> oldest_sealed() const noexcept;

    private:
        struct sealed_batch
        {
            uint64_t id{0};
            // Absolute position one past the batch's last byte; the
            // tail moves here when the batch is retired.
            uint64_t end{0};
        };

        uint64_t m_capacity{0};
        uint64_t m_head{0};
        uint64_t m_tail{0};
        uint64_t m_open_begin{0};
        std::deque<sealed_batch> m_sealed;
    };
} // namespace rendering_engine::gpu::backend::vulkan
