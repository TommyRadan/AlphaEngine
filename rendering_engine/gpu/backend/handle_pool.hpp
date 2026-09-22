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
 * @file handle_pool.hpp
 * @brief Generation-counted slot allocator shared by every concrete
 *        @ref rendering_engine::gpu::device backend.
 *
 * Header-only because it is a template. The encoded handle ID is
 * @c (generation << 32 | (slot_index + 1)) — the @c +1 reserves zero as
 * the "invalid" marker so a default-constructed handle never resolves
 * to a live slot.
 *
 * Two guarantees the backends lean on:
 *
 *   - A pointer returned by @ref lookup stays valid until that slot is
 *     removed or the pool is cleared, however many inserts happen in
 *     between. The slots live in a @c std::deque, whose @c push_back
 *     never relocates existing elements (unlike @c std::vector).
 *   - A slot's generation only ever increases. @ref clear bumps every
 *     generation rather than resetting the table, so a handle minted
 *     before a @c quit() / @c init() cycle can never resolve to a
 *     resource the next lifetime happens to place in the same slot.
 */

#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace rendering_engine::gpu::backend
{
    template<typename T>
    class handle_pool
    {
    public:
        uint64_t insert(T value)
        {
            uint32_t slot = 0;
            if (!m_free.empty())
            {
                slot = m_free.back();
                m_free.pop_back();
                m_slots[slot] = std::move(value);
                m_alive[slot] = true;
            }
            else
            {
                slot = static_cast<uint32_t>(m_slots.size());
                m_slots.push_back(std::move(value));
                m_alive.push_back(true);
                // After clear() the generation table outlives the slots,
                // so a slot index that was in use before keeps its bumped
                // generation instead of starting over at 1.
                if (slot >= m_generations.size())
                {
                    m_generations.push_back(1);
                }
            }
            return encode(slot, m_generations[slot]);
        }

        T* lookup(uint64_t encoded)
        {
            uint32_t slot = 0;
            uint32_t gen = 0;
            decode(encoded, slot, gen);
            if (slot >= m_slots.size() || !m_alive[slot] || m_generations[slot] != gen)
            {
                return nullptr;
            }
            return &m_slots[slot];
        }

        bool remove(uint64_t encoded)
        {
            uint32_t slot = 0;
            uint32_t gen = 0;
            decode(encoded, slot, gen);
            if (slot >= m_slots.size() || !m_alive[slot] || m_generations[slot] != gen)
            {
                return false;
            }
            m_alive[slot] = false;
            m_generations[slot]++;
            m_free.push_back(slot);
            return true;
        }

        // Iterate live entries. Used during quit() to release
        // any backend objects that callers leaked.
        template<typename Fn>
        void for_each(Fn&& fn)
        {
            for (size_t i = 0; i < m_slots.size(); ++i)
            {
                if (m_alive[i])
                {
                    fn(m_slots[i]);
                }
            }
        }

        // Drop every slot but keep (and advance) the generation of each
        // slot index that has ever been used, so no handle issued before
        // the clear resolves against whatever the pool hands out next.
        void clear()
        {
            for (auto& generation : m_generations)
            {
                ++generation;
            }
            m_slots.clear();
            m_alive.clear();
            m_free.clear();
        }

    private:
        static uint64_t encode(uint32_t slot, uint32_t gen)
        {
            return (static_cast<uint64_t>(gen) << 32) | static_cast<uint64_t>(slot + 1);
        }
        static void decode(uint64_t encoded, uint32_t& slot, uint32_t& gen)
        {
            if (encoded == 0)
            {
                slot = static_cast<uint32_t>(-1);
                gen = 0;
                return;
            }
            slot = static_cast<uint32_t>(encoded & 0xFFFFFFFFu) - 1;
            gen = static_cast<uint32_t>(encoded >> 32);
        }

        // deque, not vector: lookup() hands out pointers into this
        // container and a growing vector would relocate them.
        std::deque<T> m_slots;
        std::vector<uint32_t> m_generations;
        std::vector<bool> m_alive;
        std::vector<uint32_t> m_free;
    };
} // namespace rendering_engine::gpu::backend
