// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file spsc_queue.hpp
 * @brief Bounded lock-free queue between one producer thread and one consumer thread.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace core
{
    /**
     * @brief A fixed-capacity, lock-free ring buffer for exactly one producer
     *        thread and one consumer thread.
     *
     * The producer calls @ref try_push, the consumer @ref try_pop; neither
     * blocks, locks or allocates (the storage is sized once, at
     * construction). A push that finds the ring full fails and leaves it
     * unchanged, so the producer decides what to do with the value. Values
     * are copied in and out, so @p T should be a small record.
     *
     * Each side publishes its index with a release store and reads the
     * other's with an acquire load, so everything the producer wrote before
     * a push is visible to the consumer that pops the value. Which thread
     * plays either role may change only across a synchronisation point
     * between the old and the new thread (a join, a mutex hand-off).
     */
    template<typename T>
    struct spsc_queue
    {
        /** @param capacity Maximum number of queued values, rounded up to a power of two. */
        explicit spsc_queue(std::size_t capacity) : m_slots(round_up(capacity)), m_mask{m_slots.size() - 1} {}

        /** @brief Producer side: appends @p value, or returns false when the ring is full. */
        bool try_push(const T& value) noexcept
        {
            const std::size_t tail = m_tail.load(std::memory_order_relaxed);
            if (tail - m_head_seen == m_slots.size())
            {
                m_head_seen = m_head.load(std::memory_order_acquire);
                if (tail - m_head_seen == m_slots.size())
                {
                    return false;
                }
            }
            m_slots[tail & m_mask] = value;
            m_tail.store(tail + 1, std::memory_order_release);
            return true;
        }

        /** @brief Consumer side: moves the oldest value into @p out, or returns false when the ring is empty. */
        bool try_pop(T& out) noexcept
        {
            const std::size_t head = m_head.load(std::memory_order_relaxed);
            if (head == m_tail_seen)
            {
                m_tail_seen = m_tail.load(std::memory_order_acquire);
                if (head == m_tail_seen)
                {
                    return false;
                }
            }
            out = m_slots[head & m_mask];
            m_head.store(head + 1, std::memory_order_release);
            return true;
        }

    private:
        static std::size_t round_up(std::size_t capacity) noexcept
        {
            std::size_t size = 1;
            while (size < capacity)
            {
                size <<= 1;
            }
            return size;
        }

        std::vector<T> m_slots;
        std::size_t m_mask;
        std::atomic<std::size_t> m_head{0}; // next slot to pop; written by the consumer
        std::atomic<std::size_t> m_tail{0}; // next slot to push; written by the producer
        std::size_t m_head_seen{0};         // the producer's last read of m_head
        std::size_t m_tail_seen{0};         // the consumer's last read of m_tail
    };
} // namespace core
