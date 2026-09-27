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
 * @file script_host.hpp
 * @brief The scripting subsystem: the Lua state every scripted behaviour
 *        runs in.
 *
 * The host is the engine side of @ref runtime::lua_behavior. It owns one Lua
 * state with a small, safe library (base, string, table, math, utf8 and
 * coroutine; no io, os, package or debug, and no way to load a file or
 * bytecode from a script) plus the engine API (listed in
 * runtime/scripting/lua_behavior.hpp), loads each script once, shares it
 * between the behaviours that run it, and in Debug builds reloads a script
 * whose file changes on disk.
 *
 * Owned by @ref runtime::engine as @c engine::scripts, with the usual
 * @ref init / @ref quit shape. It is brought up before the scenes, so a
 * game module or a scene file can attach scripted behaviours, and taken
 * down after them, once every behaviour is gone. Neither Lua nor sol2
 * appears in this header: only the scripting implementation units compile
 * them. Main-thread-only.
 */

#pragma once

#include <memory>

#include <core/subscription.hpp>

namespace core
{
    struct event_bus;
}

namespace runtime
{
    /**
     * @brief Owns the Lua state and the loaded scripts; see the file notes.
     *
     * **Hot reload.** In Debug builds the host watches the directory of every
     * script it has loaded from a real file (through
     * @c core::platform::directory_watcher, polled twice a second from
     * @c core::render_update). When a script's file changes it is loaded
     * again and every behaviour running it is re-bound to the new code: the
     * @c self state is kept, a property still declared keeps its value, a
     * new one starts at its default and one no longer declared is cleared.
     * A behaviour an error had stopped runs again. A script that fails to
     * load leaves the previous version running. Each reload is logged.
     */
    struct script_host
    {
        /** @brief The Lua side, defined by the scripting implementation (runtime/scripting/lua_state.hpp). */
        struct state;

        script_host();
        ~script_host();

        script_host(const script_host&) = delete;
        script_host& operator=(const script_host&) = delete;
        script_host(script_host&&) = delete;
        script_host& operator=(script_host&&) = delete;

        /**
         * @brief Creates the Lua state, opens its libraries, binds the engine
         *        API and, in Debug builds, starts polling for script changes
         *        on @p events.
         */
        void init(core::event_bus& events);

        /**
         * @brief Detaches every scripted behaviour still alive from its
         *        script, then closes the Lua state. Scripted behaviours
         *        outliving the host do nothing from then on.
         */
        void quit();

        /** @brief The Lua side, or @c nullptr outside @ref init / @ref quit. */
        state* get_state() noexcept;

    private:
        std::unique_ptr<state> m_state;
        core::subscription m_reload_poll;
    };
} // namespace runtime
