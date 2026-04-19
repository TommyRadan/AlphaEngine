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

#include <utility>

#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>

void event_engine::event_bus::init()
{
    LOG_INF("Init Event Engine");
}

void event_engine::event_bus::quit()
{
    LOG_INF("Quit Event Engine: %zu event types had listeners registered", m_listeners.size());
    m_listeners.clear();
    m_pending.clear();
}

void event_engine::event_bus::dispatch(std::type_index type, const std::any& payload)
{
    auto it = m_listeners.find(type);
    if (it == m_listeners.end())
    {
        return;
    }

    for (const auto& listener : it->second)
    {
        if (!listener)
        {
            LOG_ERR("Skipping null listener for event type '%s'", type.name());
            continue;
        }
        listener(payload);
    }
}

void event_engine::event_bus::flush()
{
    // Swap the pending queue into a local so that listeners re-entering via
    // enqueue() during this flush have their events deferred to the next
    // flush, matching the buffered-dispatch contract.
    std::vector<queued_entry> draining;
    draining.swap(m_pending);

    for (const auto& entry : draining)
    {
        dispatch(entry.first, entry.second);
    }
}
