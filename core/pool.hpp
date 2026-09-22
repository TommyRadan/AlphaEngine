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
 * @file pool.hpp
 * @brief Generational slot pool and the opaque handle that indexes it.
 *
 * The building block for the "subsystem owns the storage, the caller holds a
 * handle" ownership model. A subsystem keeps its objects in a @ref pool and
 * hands out @ref pool_handle values; callers store the handle, not a pointer,
 * so the subsystem stays free to relocate or recycle storage. Each slot
 * carries a generation counter that is bumped on @ref pool::erase, so a handle
 * to an erased (and possibly recycled) slot is detected as stale on the next
 * @ref pool::get rather than silently aliasing an unrelated object — the same
 * scheme @ref rendering_engine::gpu::handle uses for GPU resources.
 *
 * Main-thread-only; not synchronised.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace core
{
    /**
     * @brief Opaque index into a @ref pool.
     *
     * The @c Tag template parameter makes handles from different pools
     * distinct types, so a handle into one pool cannot be passed where a
     * handle into another is expected. A default-constructed handle
     * (@c generation == 0) is invalid and never names a live slot.
     */
    template<typename Tag>
    struct pool_handle
    {
        uint32_t index{0};
        uint32_t generation{0};

        constexpr bool valid() const noexcept
        {
            return generation != 0;
        }

        constexpr bool operator==(const pool_handle& other) const noexcept
        {
            return index == other.index && generation == other.generation;
        }

        constexpr bool operator!=(const pool_handle& other) const noexcept
        {
            return !(*this == other);
        }
    };

    // Test-only backdoor, defined by the unit tests: lets them push a slot's
    // generation to the wrap boundary without four billion erase/insert cycles.
    struct pool_test_access;

    /**
     * @brief Owning store of @c T values addressed by stable @ref pool_handle.
     *
     * Inserted values keep their handle for as long as they live; @ref erase
     * destroys the value in place, frees the slot, and bumps its generation so
     * outstanding handles become stale. Freed slot indices are recycled through
     * a free list, so the backing storage grows to the high-water mark and is
     * then reused. The generation skips 0 (the invalid handle) when it wraps,
     * so a slot stays reachable however often it is recycled; the only thing
     * a wrap costs is that a handle from exactly 2^32 recycles earlier would
     * alias, which no caller keeps a handle long enough to hit.
     *
     * @c T only needs to be move-constructible (for storage growth); it need
     * not be default-constructible, copyable, or assignable. A value is
     * constructed by @ref insert and destroyed by @ref erase or the pool's
     * destructor — never earlier, never later.
     *
     * Live values can be walked in slot order with @ref for_each or the
     * @ref begin / @ref end iterators, which skip free slots. That order is
     * neither insertion order nor stable across an erase-then-insert (the freed
     * slot is reused first). This is the basis for component queries: "every
     * @c T in the scene" is one pass over the pool's contiguous storage rather
     * than a chase through the owning nodes.
     *
     * Pointers returned by @ref get, and iterators, are invalidated by any
     * @ref insert that grows the storage; re-resolve the handle rather than
     * holding them across inserts.
     *
     * The @c Tag parameter defaults to @c T but can be overridden when two
     * pools hold the same value type yet want incompatible handle types.
     */
    template<typename T, typename Tag = T>
    struct pool
    {
        using handle = pool_handle<Tag>;

        /**
         * @brief Forward iterator over the live values, skipping free slots.
         *
         * Dereferences to the value; @ref handle names the slot it currently
         * sits on, for callers that want to erase or remember what they found.
         * Erasing the element an iterator sits on is fine as long as it is not
         * dereferenced again before advancing.
         */
        template<bool Const>
        struct basic_iterator
        {
            using iterator_category = std::forward_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = std::conditional_t<Const, const T*, T*>;
            using reference = std::conditional_t<Const, const T&, T&>;

            basic_iterator() = default;

            reference operator*() const noexcept
            {
                return *m_pool->m_slots[m_index].value;
            }

            pointer operator->() const noexcept
            {
                return std::addressof(**this);
            }

            /** @brief Handle of the slot this iterator currently names. */
            typename pool::handle handle() const noexcept
            {
                return typename pool::handle{static_cast<uint32_t>(m_index), m_pool->m_slots[m_index].generation};
            }

            basic_iterator& operator++() noexcept
            {
                ++m_index;
                skip_free();
                return *this;
            }

            basic_iterator operator++(int) noexcept
            {
                basic_iterator copy = *this;
                ++*this;
                return copy;
            }

            bool operator==(const basic_iterator& other) const noexcept
            {
                return m_index == other.m_index;
            }

            bool operator!=(const basic_iterator& other) const noexcept
            {
                return !(*this == other);
            }

        private:
            friend struct pool;

            using pool_pointer = std::conditional_t<Const, const pool*, pool*>;

            basic_iterator(pool_pointer owner, std::size_t index) noexcept : m_pool{owner}, m_index{index}
            {
                skip_free();
            }

            void skip_free() noexcept
            {
                while (m_index < m_pool->m_slots.size() && !m_pool->m_slots[m_index].value.has_value())
                {
                    ++m_index;
                }
            }

            pool_pointer m_pool{nullptr};
            std::size_t m_index{0};
        };

        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        /** @brief Stores @p value and returns a handle naming its slot. */
        handle insert(const T& value)
        {
            return emplace_slot(value);
        }

        /** @brief Move-stores @p value and returns a handle naming its slot. */
        handle insert(T&& value)
        {
            return emplace_slot(std::move(value));
        }

        /** @brief True when @p h names a live slot in this pool. */
        bool contains(handle h) const noexcept
        {
            return live_slot(h) != nullptr;
        }

        /** @brief Pointer to the value named by @p h, or @c nullptr if stale. */
        T* get(handle h) noexcept
        {
            slot* s = live_slot(h);
            return s != nullptr ? std::addressof(*s->value) : nullptr;
        }

        /** @brief Const pointer to the value named by @p h, or @c nullptr if stale. */
        const T* get(handle h) const noexcept
        {
            const slot* s = live_slot(h);
            return s != nullptr ? std::addressof(*s->value) : nullptr;
        }

        /**
         * @brief Destroys the value named by @p h and frees its slot.
         *
         * No-op if @p h is already stale.
         */
        void erase(handle h) noexcept
        {
            slot* s = live_slot(h);
            if (s == nullptr)
            {
                return;
            }

            // Destroy in place: the value's destructor runs now, not when the
            // slot is next reused or the pool dies.
            s->value.reset();
            // Bump so any other handle to this slot reads as stale; the next
            // insert reuses the index with the new (already-bumped) generation.
            // Generation 0 is the invalid handle, so skip it on wrap — a slot
            // that landed on 0 would hand out handles nothing can resolve and
            // be lost for good.
            if (++s->generation == 0)
            {
                s->generation = 1;
            }
            m_free.push_back(h.index);
            --m_live;
        }

        /** @brief Number of live values. */
        std::size_t size() const noexcept
        {
            return m_live;
        }

        /** @brief True when no values are live. */
        bool empty() const noexcept
        {
            return m_live == 0;
        }

        /** @brief Invokes @c fn(value) on every live value, in slot order. */
        template<typename Fn>
        void for_each(Fn&& fn)
        {
            for (slot& s : m_slots)
            {
                if (s.value.has_value())
                {
                    fn(*s.value);
                }
            }
        }

        /** @brief Invokes @c fn(const value) on every live value, in slot order. */
        template<typename Fn>
        void for_each(Fn&& fn) const
        {
            for (const slot& s : m_slots)
            {
                if (s.value.has_value())
                {
                    fn(*s.value);
                }
            }
        }

        /** @brief Iterator to the first live value, or @ref end if none. */
        iterator begin() noexcept
        {
            return iterator{this, 0};
        }

        /** @brief Past-the-end iterator. */
        iterator end() noexcept
        {
            return iterator{this, m_slots.size()};
        }

        /** @brief Const iterator to the first live value, or @ref end if none. */
        const_iterator begin() const noexcept
        {
            return const_iterator{this, 0};
        }

        /** @brief Const past-the-end iterator. */
        const_iterator end() const noexcept
        {
            return const_iterator{this, m_slots.size()};
        }

    private:
        friend struct pool_test_access;

        struct slot
        {
            std::optional<T> value; // engaged exactly while the slot is live
            uint32_t generation;
        };

        template<typename U>
        handle emplace_slot(U&& value)
        {
            if (!m_free.empty())
            {
                const uint32_t index = m_free.back();
                slot& s = m_slots[index];
                s.value.emplace(std::forward<U>(value));
                // Pop only once the value is in place, so a throwing
                // constructor leaves the slot on the free list.
                m_free.pop_back();
                ++m_live;
                return handle{index, s.generation};
            }

            const uint32_t index = static_cast<uint32_t>(m_slots.size());
            // First generation is 1 so a default-constructed (generation 0)
            // handle never matches a live slot.
            m_slots.push_back(slot{std::optional<T>{std::in_place, std::forward<U>(value)}, 1u});
            ++m_live;
            return handle{index, 1u};
        }

        slot* live_slot(handle h) noexcept
        {
            if (h.generation == 0 || h.index >= m_slots.size())
            {
                return nullptr;
            }
            slot& s = m_slots[h.index];
            return (s.value.has_value() && s.generation == h.generation) ? &s : nullptr;
        }

        const slot* live_slot(handle h) const noexcept
        {
            if (h.generation == 0 || h.index >= m_slots.size())
            {
                return nullptr;
            }
            const slot& s = m_slots[h.index];
            return (s.value.has_value() && s.generation == h.generation) ? &s : nullptr;
        }

        std::vector<slot> m_slots;
        std::vector<uint32_t> m_free;
        std::size_t m_live{0};
    };
} // namespace core
