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

/**
 * @file event_engine.hpp
 * @brief Generic typed event bus: publish/subscribe hub for engine events.
 */

#pragma once

#include <any>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <event_engine/event.hpp>
#include <infrastructure/singleton.hpp>

namespace event_engine
{
    /**
     * @brief Central typed event bus used by all subsystems.
     *
     * Listeners are registered per event type @c E via @ref subscribe and
     * invoked synchronously whenever an event of that type is dispatched
     * through @ref emit. @ref enqueue buffers events for later delivery
     * through @ref flush, which drains the queue once in FIFO order.
     *
     * The bus is a process-wide singleton. Listener storage, dispatch, and
     * the pending queue are not thread-safe — register listeners during
     * init and emit/enqueue from the main-loop thread only.
     *
     * Events are arbitrary value types. No base class is required: the
     * bus keys listeners on @c std::type_index and stores enqueued events
     * in @c std::any. Game modules can introduce their own event structs
     * without touching engine headers.
     */
    struct event_bus : public singleton<event_bus>
    {
        event_bus() = default;

        /** @brief Initializes the event bus. Must be called once at startup. */
        void init();

        /** @brief Shuts down the event bus. Called once at teardown. */
        void quit();

        /**
         * @brief Registers a callback for events of type @c E.
         * @tparam E        The event struct type to subscribe to.
         * @param listener  Callable invoked on every matching dispatch.
         *                  Stored by value in the bus; any captured state
         *                  must outlive the bus or be owned by the callable.
         */
        template<typename E>
        void subscribe(std::function<void(const E&)> listener);

        /**
         * @brief Constructs an event of type @c E in place and dispatches
         *        it synchronously to every subscribed listener.
         * @tparam E       The event struct type to emit.
         * @tparam Args    Argument types forwarded to @c E's constructor.
         * @param args     Arguments forwarded to @c E's constructor.
         */
        template<typename E, typename... Args>
        void emit(Args&&... args);

        /**
         * @brief Constructs an event of type @c E in place and buffers it
         *        on the pending queue for later delivery via @ref flush.
         * @tparam E       The event struct type to enqueue.
         * @tparam Args    Argument types forwarded to @c E's constructor.
         * @param args     Arguments forwarded to @c E's constructor.
         */
        template<typename E, typename... Args>
        void enqueue(Args&&... args);

        /**
         * @brief Drains the pending queue in FIFO order, dispatching each
         *        buffered event through its registered listeners. Events
         *        enqueued while flushing are deferred until the next
         *        @ref flush call.
         */
        void flush();

    private:
        using listener_entry = std::function<void(const std::any&)>;
        using queued_entry = std::pair<std::type_index, std::any>;

        std::unordered_map<std::type_index, std::vector<listener_entry>> m_listeners;
        std::vector<queued_entry> m_pending;

        void dispatch(std::type_index type, const std::any& payload);
    };

    template<typename E>
    void event_bus::subscribe(std::function<void(const E&)> listener)
    {
        if (!listener)
        {
            return;
        }

        auto wrapper = [listener = std::move(listener)](const std::any& payload)
        { listener(*std::any_cast<E>(&payload)); };
        m_listeners[std::type_index(typeid(E))].push_back(std::move(wrapper));
    }

    template<typename E, typename... Args>
    void event_bus::emit(Args&&... args)
    {
        std::any payload = E{std::forward<Args>(args)...};
        dispatch(std::type_index(typeid(E)), payload);
    }

    template<typename E, typename... Args>
    void event_bus::enqueue(Args&&... args)
    {
        m_pending.emplace_back(std::type_index(typeid(E)), std::any{E{std::forward<Args>(args)...}});
    }
} // namespace event_engine
