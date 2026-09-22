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
 * @file subscription.hpp
 * @brief RAII token for a listener registered on the @ref core::event_bus.
 */

#pragma once

#include <cstdint>
#include <memory>

namespace core
{
    struct event_bus;

    /**
     * @brief Owning handle for one listener registered through
     *        @ref event_bus::subscribe.
     *
     * Destroying or @ref reset -ing the token unsubscribes its listener;
     * @ref release detaches it instead, leaving the listener registered
     * until the bus quits or @ref event_bus::unsubscribe is called with
     * the returned id. Movable, not copyable. A default-constructed
     * token is empty and does nothing.
     *
     * Discarding the token returned by @c subscribe unsubscribes at
     * once, so the listener would never fire — hence @c [[nodiscard]].
     * A deliberately permanent listener spells that out with
     * @c .release().
     *
     * The token resolves its bus through a weak reference, so one that
     * outlives the bus (static storage, a listener capture torn down
     * together with the bus) degrades to a no-op instead of touching
     * freed memory.
     *
     * Kept apart from event_engine.hpp so a header can hold a token as a
     * member without pulling the bus in.
     */
    struct [[nodiscard]] subscription
    {
        subscription() noexcept = default;
        ~subscription();

        subscription(const subscription&) = delete;
        subscription& operator=(const subscription&) = delete;
        subscription(subscription&& other) noexcept;
        subscription& operator=(subscription&& other) noexcept;

        /**
         * @brief Unsubscribes the listener now and empties the token.
         *        No-op when the token is empty or its bus is gone.
         */
        void reset();

        /**
         * @brief Detaches the token without unsubscribing: the listener
         *        stays registered and the token becomes empty.
         * @return The listener id, for a later manual
         *         @ref event_bus::unsubscribe; 0 when the token was empty.
         */
        std::uint64_t release() noexcept;

        /** @brief The listener id this token owns; 0 when empty. */
        std::uint64_t id() const noexcept;

        /** @brief True while the token owns a listener id. */
        explicit operator bool() const noexcept;

    private:
        friend struct event_bus;

        subscription(std::weak_ptr<event_bus*> bus, std::uint64_t id) noexcept;

        std::weak_ptr<event_bus*> m_bus;
        std::uint64_t m_id{0};
    };
} // namespace core
