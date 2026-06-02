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

#define INTERNAL_GAMEMODULE_IMPLEMENTATION
#include "game_module.hpp"
#undef INTERNAL_GAMEMODULE_IMPLEMENTATION

#include <vector>

#include <core/event_engine.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Game modules self-register at static-init time via GAME_MODULE(),
    // which runs before main() and therefore before runtime::engine is
    // constructed. We stash their info in this function-local static and
    // flush it into the live event bus once the engine is up.
    std::vector<game_module_info>& pending_modules()
    {
        static std::vector<game_module_info> storage;
        return storage;
    }

    void install_callbacks(core::event_bus& bus, const game_module_info& info)
    {
        if (info.on_engine_start)
        {
            bus.subscribe<core::engine_start>(info.on_engine_start);
        }
        if (info.on_engine_stop)
        {
            bus.subscribe<core::engine_stop>(info.on_engine_stop);
        }
        if (info.on_frame)
        {
            bus.subscribe<core::frame>(info.on_frame);
        }
        if (info.on_render_scene)
        {
            bus.subscribe<core::render_scene>(info.on_render_scene);
        }
        if (info.on_render_ui)
        {
            bus.subscribe<core::render_ui>(info.on_render_ui);
        }
        if (info.on_mouse_key_down)
        {
            bus.subscribe<core::mouse_key_down>(info.on_mouse_key_down);
        }
        if (info.on_mouse_key_up)
        {
            bus.subscribe<core::mouse_key_up>(info.on_mouse_key_up);
        }
        if (info.on_key_down)
        {
            bus.subscribe<core::key_down>(info.on_key_down);
        }
        if (info.on_key_up)
        {
            bus.subscribe<core::key_up>(info.on_key_up);
        }
        if (info.on_mouse_move)
        {
            bus.subscribe<core::mouse_move>(info.on_mouse_move);
        }
    }
} // namespace

void register_game_module(struct game_module_info& info)
{
    // Defer: runtime::engine isn't built yet during static init.
    pending_modules().push_back(info);
}

void install_pending_game_modules()
{
    auto& bus = *runtime::current_engine().events;
    for (const auto& info : pending_modules())
    {
        install_callbacks(bus, info);
    }
    pending_modules().clear();
}
