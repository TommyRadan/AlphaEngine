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

#include <stdexcept>

#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>

void event_engine::context::init()
{
    LOG_INF("Init Event Engine");
}

void event_engine::context::quit()
{
    LOG_INF("Quit Event Engine: %zu event types had listeners registered", m_listeners.size());
}

void event_engine::context::broadcast(const event& event)
{
    // Lifecycle and low-frequency events are worth noting at INFO; per-frame / input
    // events happen many times per second and would flood logs, so we stay silent for
    // them. Broadcasting to zero listeners is legal and intentionally not logged.
    switch (event.m_type)
    {
    case event_type::engine_start:
    case event_type::engine_stop:
    case event_type::quit_requested:
        LOG_INF("Broadcasting event_type=%d", static_cast<int>(event.m_type));
        break;
    default:
        break;
    }

    for (const auto& listener : m_listeners[event.m_type])
    {
        if (!listener)
        {
            LOG_ERR("Skipping null listener for event_type=%d", static_cast<int>(event.m_type));
            continue;
        }
        listener(event);
    }
}

void event_engine::context::register_listener(const event_type type, const std::function<void(const event&)>& listener)
{
    if (!listener)
    {
        LOG_ERR("Attempted to register null listener for event_type=%d", static_cast<int>(type));
        return;
    }

    m_listeners[type].push_back(listener);
    // Registration happens at static-init time (from GAME_MODULE()) and again after
    // main() runs, so keep this quiet at INFO rather than spamming WARN.
    LOG_INF("Registered listener for event_type=%d (listeners now=%zu)",
            static_cast<int>(type),
            m_listeners[type].size());
}
