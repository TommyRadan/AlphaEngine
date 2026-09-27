// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/gpu/backend/vulkan/vk_staging_ring.hpp>

namespace rendering_engine::gpu::backend::vulkan
{
    namespace
    {
        uint64_t align_up(uint64_t value, uint64_t alignment)
        {
            const uint64_t remainder = value % alignment;
            return remainder == 0 ? value : value + (alignment - remainder);
        }
    } // namespace

    staging_ring::staging_ring(uint64_t capacity) : m_capacity{capacity} {}

    std::optional<uint64_t> staging_ring::allocate(uint64_t size, uint64_t alignment)
    {
        if (size == 0 || m_capacity == 0 || size > m_capacity)
        {
            return std::nullopt;
        }
        if (alignment == 0)
        {
            alignment = 1;
        }

        // Where the reservation would start on the current lap, once
        // aligned; if it would run past the end of the buffer, the
        // next lap starts at offset 0 and the rest of this lap is
        // skipped (and stays counted as used until retired).
        const uint64_t lap_start = m_head - (m_head % m_capacity);
        const uint64_t offset = align_up(m_head - lap_start, alignment);
        uint64_t position = lap_start + offset;
        if (offset + size > m_capacity)
        {
            position = lap_start + m_capacity;
        }

        // Everything from the tail to the end of this reservation has
        // to fit in the buffer at once, or the reservation would overrun
        // bytes an unretired batch still reads.
        if (position + size - m_tail > m_capacity)
        {
            return std::nullopt;
        }
        m_head = position + size;
        return position % m_capacity;
    }

    bool staging_ring::seal(uint64_t batch_id)
    {
        if (m_head == m_open_begin)
        {
            return false;
        }
        m_sealed.push_back({batch_id, m_head});
        m_open_begin = m_head;
        return true;
    }

    void staging_ring::retire(uint64_t batch_id)
    {
        while (!m_sealed.empty() && m_sealed.front().id <= batch_id)
        {
            m_tail = m_sealed.front().end;
            m_sealed.pop_front();
        }
    }

    uint64_t staging_ring::capacity() const noexcept
    {
        return m_capacity;
    }

    uint64_t staging_ring::used() const noexcept
    {
        return m_head - m_tail;
    }

    uint64_t staging_ring::pending() const noexcept
    {
        return m_head - m_open_begin;
    }

    size_t staging_ring::sealed_count() const noexcept
    {
        return m_sealed.size();
    }

    std::optional<uint64_t> staging_ring::oldest_sealed() const noexcept
    {
        if (m_sealed.empty())
        {
            return std::nullopt;
        }
        return m_sealed.front().id;
    }
} // namespace rendering_engine::gpu::backend::vulkan
