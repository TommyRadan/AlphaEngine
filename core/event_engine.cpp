// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <core/event_engine.hpp>
#include <core/log.hpp>

std::uint32_t core::detail::next_event_type_index() noexcept
{
    static std::atomic<std::uint32_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
}

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
    std::size_t types_with_listeners = 0;
    for (const std::unique_ptr<channel_base>& channel : m_channels)
    {
        if (channel != nullptr && channel->listener_count() > 0)
        {
            ++types_with_listeners;
        }
    }
    LOG_INF("Quit Event Engine: %zu event types had listeners registered", types_with_listeners);
    m_queued.clear();

    // Every id goes stale first, then the channels are detached from the
    // bus and die at scope exit: their listeners' captures may own tokens
    // on this bus, whose destructors call back into unsubscribe and must
    // find a consistent, empty registry.
    for (std::uint32_t index = 0; index < m_slots.size(); ++index)
    {
        if (m_slots[index].in_use)
        {
            release_slot(index);
        }
    }
    m_unsettled.clear();
    std::vector<std::unique_ptr<channel_base>> dropped;
    dropped.swap(m_channels);
}

std::uint32_t core::event_bus::acquire_slot(std::uint32_t type)
{
    std::uint32_t index = 0;
    if (!m_free_slots.empty())
    {
        index = m_free_slots.back();
        m_free_slots.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }
    listener_slot& slot = m_slots[index];
    slot.in_use = true;
    slot.channel = type;
    return index;
}

void core::event_bus::release_slot(std::uint32_t index) noexcept
{
    listener_slot& slot = m_slots[index];
    slot.in_use = false;
    slot.pending = false;
    // An id carries the generation it was issued with, so bumping it here
    // turns every earlier id for this slot into a no-op. 0 is skipped on
    // wrap to keep every id non-zero.
    if (++slot.generation == 0)
    {
        slot.generation = 1;
    }
    m_free_slots.push_back(index);
}

void core::event_bus::list_unsettled(channel_base& target)
{
    if (!target.listed)
    {
        target.listed = true;
        m_unsettled.push_back(&target);
    }
}

void core::event_bus::settle_channels()
{
    // Take the list by value: settling destroys listener callables, whose
    // destructors may subscribe or unsubscribe in turn, and those must
    // start from a fresh list.
    std::vector<channel_base*> channels;
    channels.swap(m_unsettled);
    for (channel_base* channel : channels)
    {
        channel->listed = false;
    }
    for (channel_base* channel : channels)
    {
        channel->settle(*this);
    }
}

void core::event_bus::unsubscribe(std::uint64_t id)
{
    const auto index = static_cast<std::uint32_t>(id & 0xffffffffu);
    const auto generation = static_cast<std::uint32_t>(id >> 32);
    if (id == 0 || index >= m_slots.size())
    {
        return;
    }
    const listener_slot slot = m_slots[index];
    if (!slot.in_use || slot.generation != generation)
    {
        return;
    }

    // The slot goes first: retiring may destroy the listener's callable,
    // whose captures may own tokens on this bus.
    release_slot(index);
    m_channels[slot.channel]->retire(*this, slot.position, slot.pending);
}

void core::event_bus::flush()
{
    // Swap the queue into a local so that listeners re-entering via
    // enqueue() during this flush have their events deferred to the next
    // flush, matching the buffered-dispatch contract.
    std::vector<queued_event> draining;
    draining.swap(m_queued);

    for (const queued_event& event : draining)
    {
        if (event.type < m_channels.size() && m_channels[event.type] != nullptr)
        {
            const dispatch_scope scope{*this};
            m_channels[event.type]->dispatch_queued(event.payload);
        }
    }
}
