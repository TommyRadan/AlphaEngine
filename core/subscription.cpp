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
