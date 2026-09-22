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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <core/event_engine.hpp>
#include <core/log.hpp>

// Brackets one dispatch: bumps the depth so subscribe / unsubscribe defer
// rather than mutate the registry under the walk, and applies the deferred
// changes once the outermost dispatch returns. Runs on exceptional exit too,
// so a throwing listener cannot leave the bus deferring forever.
struct core::event_bus::dispatch_scope
{
    explicit dispatch_scope(event_bus& bus) noexcept : m_bus{bus}
    {
        ++m_bus.m_dispatch_depth;
    }

    ~dispatch_scope()
    {
        if (--m_bus.m_dispatch_depth == 0)
        {
            m_bus.apply_deferred();
        }
    }

    dispatch_scope(const dispatch_scope&) = delete;
    dispatch_scope& operator=(const dispatch_scope&) = delete;

private:
    event_bus& m_bus;
};

core::event_bus::event_bus() : m_self{std::make_shared<event_bus*>(this)} {}

core::event_bus::~event_bus()
{
    // Expire the tokens before the listener storage goes: captures torn
    // down with it may own tokens on this bus, and those must no-op rather
    // than re-enter a half-destroyed registry.
    m_self.reset();
}

void core::event_bus::init()
{
    LOG_INF("Init Event Engine");
}

void core::event_bus::quit()
{
    LOG_INF("Quit Event Engine: %zu event types had listeners registered", m_listeners.size());
    m_pending_unsubscribes.clear();
    m_queued.clear();

    // Detach the listeners from the bus first and let them die at scope
    // exit: their captures may own tokens on this bus whose destructors
    // call back into unsubscribe, and those must find an empty, consistent
    // registry rather than one being cleared underneath them.
    std::vector<pending_subscribe> dropped_pending;
    dropped_pending.swap(m_pending_subscribes);
    std::unordered_map<std::type_index, std::vector<listener_entry>> dropped;
    dropped.swap(m_listeners);
}

std::uint64_t core::event_bus::add_listener(std::type_index type, listener_callback callback)
{
    const std::uint64_t id = m_next_id++;
    listener_entry entry{id, std::move(callback)};
    if (m_dispatch_depth > 0)
    {
        // Inserting now could rehash the map or reallocate the vector a
        // dispatch is walking; apply_deferred adds it once that returns.
        m_pending_subscribes.push_back({type, std::move(entry)});
    }
    else
    {
        m_listeners[type].push_back(std::move(entry));
    }
    return id;
}

bool core::event_bus::remove_listener(std::uint64_t id)
{
    // Listener counts are small (a few dozen at most), so a linear scan
    // beats keeping a second id -> type index in sync.
    for (auto& bucket : m_listeners)
    {
        std::vector<listener_entry>& listeners = bucket.second;
        const auto it = std::find_if(
            listeners.begin(), listeners.end(), [id](const listener_entry& entry) { return entry.id == id; });
        if (it == listeners.end())
        {
            continue;
        }
        // Take the callable out before erasing the slot and let it die on
        // return, once the vector is consistent again: its captures may own
        // tokens on this bus whose destructors re-enter unsubscribe.
        const listener_callback detached = std::move(it->callback);
        listeners.erase(it);
        return true;
    }
    return false;
}

bool core::event_bus::remove_pending_subscribe(std::uint64_t id)
{
    const auto it = std::find_if(m_pending_subscribes.begin(),
                                 m_pending_subscribes.end(),
                                 [id](const pending_subscribe& pending) { return pending.entry.id == id; });
    if (it == m_pending_subscribes.end())
    {
        return false;
    }
    // Same ordering as remove_listener: the callable outlives the erase.
    const listener_callback detached = std::move(it->entry.callback);
    m_pending_subscribes.erase(it);
    return true;
}

bool core::event_bus::is_removal_pending(std::uint64_t id) const
{
    return std::find(m_pending_unsubscribes.begin(), m_pending_unsubscribes.end(), id) != m_pending_unsubscribes.end();
}

void core::event_bus::unsubscribe(std::uint64_t id)
{
    if (id == 0)
    {
        return;
    }

    if (m_dispatch_depth == 0)
    {
        remove_listener(id);
        return;
    }

    // Mid-dispatch. A listener subscribed during this same dispatch has not
    // reached the registry yet, so cancel its pending add instead. Otherwise
    // record the id: the walk skips it from now on and apply_deferred erases
    // it once the outermost dispatch returns, so the callable is never
    // destroyed while it may be the one executing.
    if (remove_pending_subscribe(id) || is_removal_pending(id))
    {
        return;
    }
    m_pending_unsubscribes.push_back(id);
}

void core::event_bus::apply_deferred()
{
    // Take both lists by value: applying them can run listener destructors
    // that subscribe or unsubscribe in turn, and those must start from a
    // fresh, consistent pending state.
    std::vector<pending_subscribe> subscribes;
    subscribes.swap(m_pending_subscribes);
    std::vector<std::uint64_t> unsubscribes;
    unsubscribes.swap(m_pending_unsubscribes);

    for (pending_subscribe& pending : subscribes)
    {
        m_listeners[pending.type].push_back(std::move(pending.entry));
    }
    for (const std::uint64_t id : unsubscribes)
    {
        remove_listener(id);
    }
}

void core::event_bus::dispatch(std::type_index type, const std::any& payload)
{
    const auto bucket = m_listeners.find(type);
    if (bucket == m_listeners.end())
    {
        return;
    }

    // Every subscribe / unsubscribe is deferred while the depth is non-zero,
    // so neither the map nor this vector changes under the walk: the
    // reference and the index stay valid across listener bodies and nested
    // emits. An entry unsubscribed part-way through stays in place (its
    // callable may be the one running) and is skipped from then on.
    const dispatch_scope scope{*this};
    const std::vector<listener_entry>& listeners = bucket->second;
    for (std::size_t i = 0; i < listeners.size(); ++i)
    {
        const listener_entry& entry = listeners[i];
        if (!is_removal_pending(entry.id))
        {
            entry.callback(payload);
        }
    }
}

void core::event_bus::flush()
{
    // Swap the queue into a local so that listeners re-entering via
    // enqueue() during this flush have their events deferred to the next
    // flush, matching the buffered-dispatch contract.
    std::vector<queued_event> draining;
    draining.swap(m_queued);

    for (const auto& entry : draining)
    {
        dispatch(entry.first, entry.second);
    }
}
