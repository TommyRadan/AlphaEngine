// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file dense_pool.hpp
 * @brief Contiguous store addressed by stable generational handles that keeps
 *        its values in insertion order.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <core/pool.hpp>

namespace core
{
    /**
     * @brief Owning store of @c T values in one contiguous array, in the
     *        order they were inserted, each addressed by a stable
     *        @ref pool_handle.
     *
     * Where @ref pool recycles freed slots in place — so a walk visits values
     * in slot order, which drifts from insertion order once anything is
     * erased — this store keeps the values packed and ordered: @ref insert
     * appends, and @ref erase closes the gap by shifting the later values
     * down one place, so @ref values always lists the live values in the
     * order they were inserted. A handle reaches its value through a slot
     * that records where the value currently sits, so shifting never
     * invalidates a handle; a slot's generation is bumped when its value is
     * erased, so a handle to an erased value reads as stale, exactly as in
     * @ref pool.
     *
     * The price of the order is an O(n) @ref erase. It suits stores whose
     * walk order is part of their meaning and that are walked far more often
     * than they change, such as the renderer's proxies, whose order is the
     * order they are drawn and packed in.
     *
     * Pointers and references into the store, and the span @ref values
     * returns, are invalidated by any @ref insert or @ref erase; handles are
     * not. Main-thread-only; not synchronised.
     */
    template<typename T, typename Tag = T>
    struct dense_pool
    {
        using handle = pool_handle<Tag>;

        /** @brief Appends @p value and returns a handle naming it. */
        handle insert(T value)
        {
            uint32_t index = 0;
            if (!m_free.empty())
            {
                index = m_free.back();
                m_free.pop_back();
            }
            else
            {
                index = static_cast<uint32_t>(m_slots.size());
                // First generation is 1 so a default-constructed handle
                // (generation 0) never names a live value.
                m_slots.push_back(slot{0, 1u, false});
            }
            slot& s = m_slots[index];
            s.dense = static_cast<uint32_t>(m_values.size());
            s.live = true;
            m_values.push_back(std::move(value));
            m_owners.push_back(index);
            return handle{index, s.generation};
        }

        /**
         * @brief Destroys the value named by @p h, keeping the order of the
         *        others. No-op if @p h is stale.
         */
        void erase(handle h)
        {
            slot* s = live_slot(h);
            if (s == nullptr)
            {
                return;
            }
            const uint32_t dense = s->dense;
            m_values.erase(m_values.begin() + dense);
            m_owners.erase(m_owners.begin() + dense);
            // Every value after the erased one moved down a place.
            for (std::size_t i = dense; i < m_owners.size(); ++i)
            {
                m_slots[m_owners[i]].dense = static_cast<uint32_t>(i);
            }
            s->live = false;
            // Generation 0 is the invalid handle, so skip it on wrap.
            if (++s->generation == 0)
            {
                s->generation = 1;
            }
            m_free.push_back(h.index);
        }

        /** @brief True when @p h names a live value. */
        bool contains(handle h) const noexcept
        {
            return live_slot(h) != nullptr;
        }

        /** @brief The value named by @p h, or @c nullptr if stale. */
        T* get(handle h) noexcept
        {
            const slot* s = live_slot(h);
            return s != nullptr ? &m_values[s->dense] : nullptr;
        }

        /** @brief The value named by @p h, or @c nullptr if stale. */
        const T* get(handle h) const noexcept
        {
            const slot* s = live_slot(h);
            return s != nullptr ? &m_values[s->dense] : nullptr;
        }

        /** @brief Every live value, in insertion order. */
        std::span<T> values() noexcept
        {
            return m_values;
        }

        /** @brief Every live value, in insertion order. */
        std::span<const T> values() const noexcept
        {
            return m_values;
        }

        /** @brief The handle of the value at position @p position of @ref values. */
        handle handle_at(std::size_t position) const noexcept
        {
            const uint32_t index = m_owners[position];
            return handle{index, m_slots[index].generation};
        }

        /** @brief Number of live values. */
        std::size_t size() const noexcept
        {
            return m_values.size();
        }

        /** @brief True when no values are live. */
        bool empty() const noexcept
        {
            return m_values.empty();
        }

    private:
        struct slot
        {
            // Position of the slot's value in m_values while live.
            uint32_t dense;
            uint32_t generation;
            bool live;
        };

        const slot* live_slot(handle h) const noexcept
        {
            if (h.generation == 0 || h.index >= m_slots.size())
            {
                return nullptr;
            }
            const slot& s = m_slots[h.index];
            return (s.live && s.generation == h.generation) ? &s : nullptr;
        }

        slot* live_slot(handle h) noexcept
        {
            return const_cast<slot*>(std::as_const(*this).live_slot(h));
        }

        std::vector<T> m_values;
        // Parallel to m_values: the slot each value belongs to.
        std::vector<uint32_t> m_owners;
        std::vector<slot> m_slots;
        std::vector<uint32_t> m_free;
    };
} // namespace core
