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
#include <utility>
#include <vector>

namespace infrastructure
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

    /**
     * @brief Owning store of @c T values addressed by stable @ref pool_handle.
     *
     * Inserted values keep their handle for as long as they live; @ref erase
     * frees the slot and bumps its generation so outstanding handles become
     * stale. Freed slot indices are recycled through a free list, so the
     * backing storage grows to the high-water mark and is then reused. The
     * @c Tag parameter defaults to @c T but can be overridden when two pools
     * hold the same value type yet want incompatible handle types.
     */
    template<typename T, typename Tag = T>
    struct pool
    {
        using handle = pool_handle<Tag>;

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
            return s != nullptr ? &s->value : nullptr;
        }

        /** @brief Const pointer to the value named by @p h, or @c nullptr if stale. */
        const T* get(handle h) const noexcept
        {
            const slot* s = live_slot(h);
            return s != nullptr ? &s->value : nullptr;
        }

        /** @brief Frees the slot named by @p h. No-op if @p h is already stale. */
        void erase(handle h) noexcept
        {
            slot* s = live_slot(h);
            if (s == nullptr)
            {
                return;
            }

            s->value = T{};
            s->alive = false;
            // Bump so any other handle to this slot reads as stale; the next
            // insert reuses the index with the new (already-bumped) generation.
            ++s->generation;
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

    private:
        struct slot
        {
            T value;
            uint32_t generation;
            bool alive;
        };

        template<typename U>
        handle emplace_slot(U&& value)
        {
            if (!m_free.empty())
            {
                uint32_t index = m_free.back();
                m_free.pop_back();
                slot& s = m_slots[index];
                s.value = std::forward<U>(value);
                s.alive = true;
                ++m_live;
                return handle{index, s.generation};
            }

            uint32_t index = static_cast<uint32_t>(m_slots.size());
            // First generation is 1 so a default-constructed (generation 0)
            // handle never matches a live slot.
            m_slots.push_back(slot{std::forward<U>(value), 1u, true});
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
            return (s.alive && s.generation == h.generation) ? &s : nullptr;
        }

        const slot* live_slot(handle h) const noexcept
        {
            if (h.generation == 0 || h.index >= m_slots.size())
            {
                return nullptr;
            }
            const slot& s = m_slots[h.index];
            return (s.alive && s.generation == h.generation) ? &s : nullptr;
        }

        std::vector<slot> m_slots;
        std::vector<uint32_t> m_free;
        std::size_t m_live{0};
    };
} // namespace infrastructure
