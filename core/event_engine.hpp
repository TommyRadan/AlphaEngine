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
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <core/event.hpp>
#include <core/subscription.hpp>

namespace core
{
    /**
     * @brief Central typed event bus used by all subsystems.
     *
     * Listeners are registered per event type @c E via @ref subscribe and
     * invoked synchronously, in subscription order, whenever an event of
     * that type is dispatched through @ref emit. @ref enqueue buffers
     * events for later delivery through @ref flush, which drains the
     * queue once in FIFO order; @ref runtime::engine::tick flushes once
     * per tick, right after the window has pumped input and before the
     * fixed-step updates, so an enqueued event reaches its listeners at
     * the start of the next tick.
     *
     * @ref subscribe hands back a @ref subscription token that
     * unsubscribes the listener when it is destroyed (or reset); a
     * listener meant to live as long as the bus is detached explicitly
     * with @c release(). @ref unsubscribe removes a listener by id for
     * callers that manage lifetimes by hand.
     *
     * Dispatch is reentrant. A listener may emit (nested dispatch) and
     * may subscribe or unsubscribe any listener, itself included. While
     * a dispatch is in progress those registry changes are deferred and
     * applied once the outermost dispatch returns, so the listener
     * storage never changes under a walk: a listener subscribed during a
     * dispatch first fires on the next emit of its type after that
     * dispatch has returned, and one unsubscribed during a dispatch is
     * skipped for the remainder of it and destroyed only after it —
     * never while it may be executing.
     *
     * Owned by @ref runtime::engine. Listener storage, dispatch, and the
     * queues are not thread-safe — subscribe, emit, enqueue and flush
     * from the main-loop thread only.
     *
     * Events are arbitrary value types. No base class is required: the
     * bus keys listeners on @c std::type_index and stores enqueued events
     * in @c std::any. Game modules can introduce their own event structs
     * without touching engine headers.
     */
    struct event_bus
    {
        event_bus();
        ~event_bus();

        // Tokens resolve the bus through a weak reference to its address,
        // so an instance must stay put for its whole lifetime.
        event_bus(const event_bus&) = delete;
        event_bus& operator=(const event_bus&) = delete;
        event_bus(event_bus&&) = delete;
        event_bus& operator=(event_bus&&) = delete;

        /** @brief Initializes the event bus. Must be called once at startup. */
        void init();

        /**
         * @brief Shuts down the event bus, dropping every listener and
         *        queued event. Called once at teardown; tokens still held
         *        afterwards are harmless.
         */
        void quit();

        /**
         * @brief Registers a callback for events of type @c E.
         * @tparam E        The event struct type to subscribe to.
         * @param listener  Callable invoked on every matching dispatch.
         *                  Stored by value in the bus; any captured state
         *                  must outlive the subscription or be owned by
         *                  the callable.
         * @return A token that unsubscribes the listener when destroyed.
         *         Discarding it unsubscribes at once; call @c release()
         *         on it to keep the listener for the bus's lifetime.
         *         Empty when @p listener is null.
         */
        template<typename E>
        subscription subscribe(std::function<void(const E&)> listener);

        /**
         * @brief Removes the listener with the given id; a no-op for an
         *        id that is unknown, already removed, or 0.
         *
         * Called during a dispatch, the removal is deferred until the
         * outermost dispatch returns and the listener is skipped for the
         * rest of it (see the class notes on reentrancy).
         * @param id  The id from @ref subscription::id or
         *            @ref subscription::release.
         */
        void unsubscribe(std::uint64_t id);

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
         *        @ref flush call. The engine calls this once per tick.
         */
        void flush();

    private:
        using listener_callback = std::function<void(const std::any&)>;
        using queued_event = std::pair<std::type_index, std::any>;

        struct listener_entry
        {
            std::uint64_t id;
            listener_callback callback;
        };

        struct pending_subscribe
        {
            std::type_index type;
            listener_entry entry;
        };

        // Scope guard around one dispatch: maintains m_dispatch_depth and
        // applies the deferred registry changes when the outermost
        // dispatch returns. Defined in event_engine.cpp.
        struct dispatch_scope;

        std::unordered_map<std::type_index, std::vector<listener_entry>> m_listeners;
        std::vector<queued_event> m_queued;
        std::vector<pending_subscribe> m_pending_subscribes;
        std::vector<std::uint64_t> m_pending_unsubscribes;
        std::uint64_t m_next_id{1};
        int m_dispatch_depth{0};
        // Liveness anchor: tokens hold it weakly, so one that outlives the
        // bus sees an expired reference instead of a dangling pointer.
        std::shared_ptr<event_bus*> m_self;

        std::uint64_t add_listener(std::type_index type, listener_callback callback);
        bool remove_listener(std::uint64_t id);
        bool remove_pending_subscribe(std::uint64_t id);
        bool is_removal_pending(std::uint64_t id) const;
        void apply_deferred();
        void dispatch(std::type_index type, const std::any& payload);
    };

    template<typename E>
    subscription event_bus::subscribe(std::function<void(const E&)> listener)
    {
        if (!listener)
        {
            return {};
        }

        auto wrapper = [listener = std::move(listener)](const std::any& payload)
        { listener(*std::any_cast<E>(&payload)); };
        return subscription{m_self, add_listener(std::type_index(typeid(E)), std::move(wrapper))};
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
        m_queued.emplace_back(std::type_index(typeid(E)), std::any{E{std::forward<Args>(args)...}});
    }
} // namespace core
