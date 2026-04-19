/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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

#include <scene_graph/world.hpp>

#include <infrastructure/log.hpp>

namespace scene_graph
{
    entity_id world::create_entity()
    {
        // Skip over invalid_entity (0) on the extremely unlikely wrap-around.
        // With 32 bits of id space this is defensive rather than realistic.
        if (m_next_id == invalid_entity)
        {
            ++m_next_id;
        }
        const entity_id id = m_next_id++;
        m_alive.insert(id);
        return id;
    }

    void world::destroy_entity(entity_id entity)
    {
        if (entity == invalid_entity)
        {
            return;
        }
        if (m_alive.erase(entity) == 0)
        {
            // Destroying an already-dead entity is benign but almost always a bug
            // in the caller; surface it at WARN so the pattern shows up in logs.
            LOG_WRN("destroy_entity called on unknown id=%u", entity);
            return;
        }
        for (auto& [key, storage] : m_components)
        {
            storage->erase(entity);
        }
    }

    bool world::is_alive(entity_id entity) const
    {
        if (entity == invalid_entity)
        {
            return false;
        }
        return m_alive.find(entity) != m_alive.end();
    }

    void world::clear()
    {
        m_alive.clear();
        m_components.clear();
        m_next_id = 1;
    }
} // namespace scene_graph
