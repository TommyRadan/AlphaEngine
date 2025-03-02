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

#include "API/GameModule.hpp"
#include "API/Log.hpp"
#include <event_engine/event_engine.hpp>

static void OnKeyDown(const event_engine::event& event)
{
    auto key_down_event = dynamic_cast<const event_engine::key_down*>(&event);
    auto keyCode = key_down_event->m_key_code;
    if (keyCode != event_engine::key_code::ESCAPE) return;
    event_engine::context::
    get_instance().broadcast(event_engine::quit_requested());
}

GAME_MODULE()
{
    struct GameModuleInfo info;
    info.onKeyDown = OnKeyDown;
    RegisterGameModule(info);
    return true;
}
