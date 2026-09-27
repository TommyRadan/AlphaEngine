// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/subscription.hpp>

#include <memory>
#include <utility>

#include <core/event_engine.hpp>

core::subscription::subscription(std::weak_ptr<event_bus*> bus, std::uint64_t id) noexcept
    : m_bus{std::move(bus)}, m_id{id}
{
}

core::subscription::~subscription()
{
    reset();
}

core::subscription::subscription(subscription&& other) noexcept
    : m_bus{std::move(other.m_bus)}, m_id{std::exchange(other.m_id, 0)}
{
}

core::subscription& core::subscription::operator=(subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_bus = std::move(other.m_bus);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

void core::subscription::reset()
{
    const std::uint64_t id = std::exchange(m_id, 0);
    // lock() fails once the bus has been destroyed, so a token that outlived
    // it no-ops instead of dereferencing freed memory.
    if (const std::shared_ptr<event_bus*> bus = m_bus.lock(); bus && id != 0)
    {
        (*bus)->unsubscribe(id);
    }
    m_bus.reset();
}

std::uint64_t core::subscription::release() noexcept
{
    m_bus.reset();
    return std::exchange(m_id, 0);
}

std::uint64_t core::subscription::id() const noexcept
{
    return m_id;
}

core::subscription::operator bool() const noexcept
{
    return m_id != 0;
}
