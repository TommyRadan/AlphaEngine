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
#include "GameModule.hpp"
#undef INTERNAL_GAMEMODULE_IMPLEMENTATION
#include <event_engine/event_engine.hpp>

void RegisterGameModule(struct GameModuleInfo &info)
{
    if (info.onEngineStart)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::engine_start, info.onEngineStart);
    }

    if (info.onEngineStop)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::engine_stop, info.onEngineStop);
    }

    if (info.onFrame)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::frame, info.onFrame);
    }

    if (info.onRenderScene)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::render_scene, info.onRenderScene);
    }

    if (info.onRenderUi)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::render_ui, info.onRenderScene);
    }

    if (info.onMouseKeyDown)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::mouse_key_down, info.onMouseKeyDown);
    }

    if (info.onMouseKeyUp)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::mouse_key_up, info.onMouseKeyUp);
    }

    if (info.onKeyDown)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::key_down, info.onKeyDown);
    }

    if (info.onKeyUp)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::key_up, info.onKeyUp);
    }

    if (info.onMouseMove)
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::mouse_move, info.onMouseMove);
    }
}
