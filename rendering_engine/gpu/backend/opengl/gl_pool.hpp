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
 * @file gl_pool.hpp
 * @brief Generation-counted slot allocator that backs every opaque
 *        @ref gpu::handle in the OpenGL backend.
 *
 * Header-only because it is a template. The encoded handle ID is
 * @c (generation << 32 | (slot_index + 1)) — the @c +1 reserves zero as
 * the "invalid" marker so a default-constructed handle never resolves
 * to a live slot.
 */

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace rendering_engine
{
    namespace gpu
    {
        namespace backend
        {
            namespace opengl
            {
                template<typename T>
                class gl_pool
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
                            m_generations.push_back(1);
                            m_alive.push_back(true);
                        }
                        const uint32_t gen = m_generations[slot];
                        return encode(slot, gen);
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
                    // any GL objects that callers leaked.
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

                    void clear()
                    {
                        m_slots.clear();
                        m_generations.clear();
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

                    std::vector<T> m_slots;
                    std::vector<uint32_t> m_generations;
                    std::vector<bool> m_alive;
                    std::vector<uint32_t> m_free;
                };
            } // namespace opengl
        } // namespace backend
    } // namespace gpu
} // namespace rendering_engine
