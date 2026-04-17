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
 * @brief Event dispatch subsystem: publish/subscribe hub for engine events.
 */

#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include <event_engine/event.hpp>
#include <infrastructure/singleton.hpp>

namespace event_engine
{
    /**
     * @brief Central event bus used by all subsystems.
     *
     * Listeners register callbacks keyed on an @ref event_type and are
     * invoked synchronously whenever an event of that type is broadcast.
     * The context is a process-wide singleton; listener storage and
     * dispatch are not thread-safe — register listeners during init and
     * broadcast from the main loop thread only.
     */
    struct context : public singleton<context>
    {
        context() = default;

        /** @brief Initializes the event engine. Must be called once at startup. */
        void init();

        /** @brief Shuts down the event engine. Called once at teardown. */
        void quit();

        /**
         * @brief Dispatches @p event synchronously to every listener
         *        registered for its type.
         * @param event Event to broadcast. The reference must stay valid
         *              for the duration of the call; listeners receive a
         *              const reference and must not retain it.
         */
        void broadcast(const event& event);

        /**
         * @brief Registers a callback for events of a given type.
         * @param type     The event type to subscribe to.
         * @param listener Callable invoked on every matching broadcast.
         *                 Stored by value in the engine; any captured
         *                 state must outlive the engine or be owned by
         *                 the callable itself.
         */
        void register_listener(const event_type type, const std::function<void(const event&)>& listener);

    private:
        std::unordered_map<event_type, std::vector<std::function<void(const event&)>>> m_listeners;
    };
} // namespace event_engine
