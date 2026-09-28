// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file event_engine.hpp
 * @brief Generic typed event bus: publish/subscribe hub for engine events.
 */

#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <core/event.hpp>
#include <core/subscription.hpp>

namespace core
{
    namespace detail
    {
        /** @brief Hands out the next event-type index; see @ref event_type_index. Thread-safe. */
        std::uint32_t next_event_type_index() noexcept;

        /**
         * @brief A dense, process-wide index for event type @p E, assigned
         *        the first time the type is used. The bus keeps its
         *        per-type listener storage in a vector indexed by it, so
         *        finding the listeners of an event is one array access.
         */
        template<typename E>
        std::uint32_t event_type_index() noexcept
        {
            static const std::uint32_t index = next_event_type_index();
            return index;
        }
    } // namespace detail

    /**
     * @brief Central typed event bus used by all subsystems.
     *
     * Listeners are registered per event type @c E via @ref subscribe and
     * invoked synchronously, in subscription order, whenever an event of
     * that type is dispatched through @ref emit. @ref enqueue buffers
     * events for later delivery through @ref flush, which drains the
     * queue once in FIFO order; the engine's input stage flushes once per
     * frame, right after the window has pumped its events, so an enqueued
     * event reaches its listeners at the start of the next frame.
     *
     * @ref subscribe hands back a @ref subscription token that
     * unsubscribes the listener when it is destroyed (or reset); a
     * listener meant to live as long as the bus is detached explicitly
     * with @c release(). @ref unsubscribe removes a listener by id for
     * callers that manage lifetimes by hand.
     *
     * **Storage.** Each event type has its own channel, found by the
     * type's dense index (@ref detail::event_type_index): its listeners in
     * subscription order, each stored as the caller's own
     * @c std::function<void(const E&)>. @ref emit builds the event on the
     * stack and hands it to them by reference, so dispatching allocates
     * nothing; only @ref enqueue type-erases an event (into a
     * @c std::any), because its queue holds events of every type. A
     * listener id names a slot in a slot map (index plus generation) that
     * records the listener's channel and position, so @ref unsubscribe
     * finds it in constant time and leaves a tombstone in its place. The
     * tombstones of a channel are compacted when a dispatch of it returns,
     * or at once when they make up half of it, so removing N listeners
     * costs O(N) in total.
     *
     * **Reentrancy.** A listener may emit (nested dispatch) and may
     * subscribe or unsubscribe any listener, itself included. While a
     * dispatch is in progress no channel changes shape: a listener
     * subscribed during it waits in its channel's pending list and first
     * fires on the next emit of its type after the outermost dispatch has
     * returned, and one unsubscribed during it is tombstoned, skipped for
     * the remainder of the dispatch, and destroyed only after it — never
     * while it may be executing. A listener's callable is always destroyed
     * once the bus is consistent again, so its captures may own tokens on
     * this bus.
     *
     * Owned by @ref runtime::engine. Listener storage, dispatch, and the
     * queue are not thread-safe — subscribe, emit, enqueue and flush from
     * the main-loop thread only.
     *
     * Events are arbitrary value types. No base class is required, so game
     * modules can introduce their own event structs without touching
     * engine headers.
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
         * @brief Removes the listener with the given id, in constant time;
         *        a no-op for an id that is unknown, already removed, or 0.
         *
         * Called during a dispatch, the listener is skipped for the rest of
         * it and destroyed once the outermost dispatch returns (see the
         * class notes on reentrancy).
         * @param id  The id from @ref subscription::id or
         *            @ref subscription::release.
         */
        void unsubscribe(std::uint64_t id);

        /**
         * @brief Constructs an event of type @c E on the stack and
         *        dispatches it synchronously to every subscribed listener.
         * @tparam E       The event struct type to emit.
         * @tparam Args    Argument types forwarded to @c E's constructor.
         * @param args     Arguments forwarded to @c E's constructor.
         */
        template<typename E, typename... Args>
        void emit(Args&&... args);

        /**
         * @brief Constructs an event of type @c E and buffers it on the
         *        pending queue for later delivery via @ref flush.
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
         *        @ref flush call. The engine calls this once per frame.
         */
        void flush();

    private:
        // Marks a tombstoned listener entry: its slot has been released.
        static constexpr std::uint32_t k_retired = 0xffffffffu;

        // Where the listener a slot's id names lives. A released slot bumps
        // its generation, so an id handed out earlier no longer matches.
        struct listener_slot
        {
            std::uint32_t generation{1};
            std::uint32_t channel{0};  // the event type's dense index
            std::uint32_t position{0}; // index in the channel's live or pending list
            bool in_use{false};
            bool pending{false};
        };

        // The listeners of one event type, behind a type-erased interface
        // for what the bus does without knowing the type.
        struct channel_base
        {
            channel_base() = default;
            virtual ~channel_base() = default;
            channel_base(const channel_base&) = delete;
            channel_base& operator=(const channel_base&) = delete;
            channel_base(channel_base&&) = delete;
            channel_base& operator=(channel_base&&) = delete;

            // Tombstones the listener at @p position. Outside a dispatch its
            // callable is destroyed before this returns, once the channel is
            // consistent; during one it lives until the channel is settled.
            virtual void retire(event_bus& bus, std::uint32_t position, bool in_pending) = 0;

            // Compacts the tombstones and appends the pending listeners.
            // Only outside a dispatch.
            virtual void settle(event_bus& bus) = 0;

            // Dispatches a queued event; @p payload holds the channel's type.
            virtual void dispatch_queued(const std::any& payload) const = 0;

            // Number of listeners, tombstones excluded.
            virtual std::size_t listener_count() const noexcept = 0;

            // Whether the channel is on the bus's list of channels to settle.
            bool listed{false};
        };

        template<typename E>
        struct channel;

        // An event buffered by enqueue: its type's dense index and a copy.
        struct queued_event
        {
            std::uint32_t type;
            std::any payload;
        };

        // Scope guard around one dispatch: maintains m_dispatch_depth and
        // settles the channels changed during it when the outermost
        // dispatch returns — on exceptional exit too, so a throwing
        // listener cannot leave the bus deferring forever.
        struct dispatch_scope
        {
            explicit dispatch_scope(event_bus& bus) noexcept : m_bus{bus}
            {
                ++m_bus.m_dispatch_depth;
            }

            ~dispatch_scope()
            {
                if (--m_bus.m_dispatch_depth == 0 && !m_bus.m_unsettled.empty())
                {
                    m_bus.settle_channels();
                }
            }

            dispatch_scope(const dispatch_scope&) = delete;
            dispatch_scope& operator=(const dispatch_scope&) = delete;
            dispatch_scope(dispatch_scope&&) = delete;
            dispatch_scope& operator=(dispatch_scope&&) = delete;

        private:
            event_bus& m_bus;
        };

        std::vector<std::unique_ptr<channel_base>> m_channels; // indexed by detail::event_type_index
        std::vector<listener_slot> m_slots;
        std::vector<std::uint32_t> m_free_slots;
        std::vector<channel_base*> m_unsettled; // channels with tombstones or pending listeners
        std::vector<queued_event> m_queued;
        int m_dispatch_depth{0};
        // Liveness anchor: tokens hold it weakly, so one that outlives the
        // bus sees an expired reference instead of a dangling pointer.
        std::shared_ptr<event_bus*> m_self;

        template<typename E>
        channel<E>* find_channel() noexcept;

        template<typename E>
        channel<E>& channel_for();

        // Takes a free slot (or a new one) for a listener on channel @p type.
        std::uint32_t acquire_slot(std::uint32_t type);
        void release_slot(std::uint32_t index) noexcept;
        void list_unsettled(channel_base& target);
        void settle_channels();

        static std::uint64_t make_id(std::uint32_t index, std::uint32_t generation) noexcept
        {
            return (static_cast<std::uint64_t>(generation) << 32) | index;
        }
    };

    template<typename E>
    struct event_bus::channel final : channel_base
    {
        struct entry
        {
            std::uint32_t slot; // k_retired once unsubscribed
            std::function<void(const E&)> callback;
        };

        std::vector<entry> live;    // subscription order, tombstones included
        std::vector<entry> pending; // subscribed during a dispatch
        std::size_t tombstones{0};  // tombstones in live

        // Calls every live listener. Nothing reshapes live while a dispatch
        // is in progress (see the class notes), so the index walk stays
        // valid across listener bodies and nested emits.
        void dispatch(const E& event) const
        {
            const std::size_t count = live.size();
            for (std::size_t i = 0; i < count; ++i)
            {
                const entry& listener = live[i];
                if (listener.slot != k_retired)
                {
                    listener.callback(event);
                }
            }
        }

        void add(event_bus& bus, std::uint32_t slot, std::function<void(const E&)> callback)
        {
            listener_slot& record = bus.m_slots[slot];
            if (bus.m_dispatch_depth > 0)
            {
                // Appending to live could reallocate it under the walk.
                record.pending = true;
                record.position = static_cast<std::uint32_t>(pending.size());
                pending.push_back(entry{slot, std::move(callback)});
                bus.list_unsettled(*this);
            }
            else
            {
                record.pending = false;
                record.position = static_cast<std::uint32_t>(live.size());
                live.push_back(entry{slot, std::move(callback)});
            }
        }

        void retire(event_bus& bus, std::uint32_t position, bool in_pending) override
        {
            entry& listener = in_pending ? pending[position] : live[position];
            listener.slot = k_retired;
            if (!in_pending)
            {
                ++tombstones;
            }
            if (bus.m_dispatch_depth > 0)
            {
                bus.list_unsettled(*this);
                return;
            }

            // Not dispatching, so the callable cannot be running: take it
            // out and let it die on return, after the channel is consistent.
            const std::function<void(const E&)> detached = std::move(listener.callback);
            listener.callback = nullptr;
            if (tombstones * 2 >= live.size())
            {
                settle(bus);
            }
            else
            {
                bus.list_unsettled(*this);
            }
        }

        void settle(event_bus& bus) override
        {
            // Both lists are rebuilt; the old storage, holding the
            // tombstones' callables, dies at the end, once every slot
            // points at its listener's new position.
            std::vector<entry> previous;
            if (tombstones > 0)
            {
                std::vector<entry> kept;
                kept.reserve(live.size() - tombstones + pending.size());
                for (entry& listener : live)
                {
                    if (listener.slot != k_retired)
                    {
                        bus.m_slots[listener.slot].position = static_cast<std::uint32_t>(kept.size());
                        kept.push_back(std::move(listener));
                    }
                }
                previous.swap(live);
                live.swap(kept);
                tombstones = 0;
            }
            std::vector<entry> added;
            added.swap(pending);
            for (entry& listener : added)
            {
                if (listener.slot != k_retired)
                {
                    listener_slot& record = bus.m_slots[listener.slot];
                    record.pending = false;
                    record.position = static_cast<std::uint32_t>(live.size());
                    live.push_back(std::move(listener));
                }
            }
        }

        void dispatch_queued(const std::any& payload) const override
        {
            dispatch(*std::any_cast<E>(&payload));
        }

        std::size_t listener_count() const noexcept override
        {
            return live.size() - tombstones;
        }
    };

    template<typename E>
    event_bus::channel<E>* event_bus::find_channel() noexcept
    {
        const std::uint32_t type = detail::event_type_index<E>();
        if (type >= m_channels.size() || m_channels[type] == nullptr)
        {
            return nullptr;
        }
        return static_cast<channel<E>*>(m_channels[type].get());
    }

    template<typename E>
    event_bus::channel<E>& event_bus::channel_for()
    {
        const std::uint32_t type = detail::event_type_index<E>();
        if (type >= m_channels.size())
        {
            // Only the owning pointers move; the channels themselves stay put.
            m_channels.resize(type + 1);
        }
        std::unique_ptr<channel_base>& owner = m_channels[type];
        if (owner == nullptr)
        {
            owner = std::make_unique<channel<E>>();
        }
        return static_cast<channel<E>&>(*owner);
    }

    template<typename E>
    subscription event_bus::subscribe(std::function<void(const E&)> listener)
    {
        if (!listener)
        {
            return {};
        }

        channel<E>& target = channel_for<E>();
        const std::uint32_t slot = acquire_slot(detail::event_type_index<E>());
        target.add(*this, slot, std::move(listener));
        return subscription{m_self, make_id(slot, m_slots[slot].generation)};
    }

    template<typename E, typename... Args>
    void event_bus::emit(Args&&... args)
    {
        const channel<E>* target = find_channel<E>();
        if (target == nullptr || target->live.empty())
        {
            return;
        }
        const E event{std::forward<Args>(args)...};
        const dispatch_scope scope{*this};
        target->dispatch(event);
    }

    template<typename E, typename... Args>
    void event_bus::enqueue(Args&&... args)
    {
        m_queued.push_back(queued_event{detail::event_type_index<E>(), std::any{E{std::forward<Args>(args)...}}});
    }
} // namespace core
