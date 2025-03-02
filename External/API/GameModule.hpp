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

#pragma once

#include <event_engine/event_engine.hpp>
#include <string>

struct GameModuleInfo
{
    std::function<void(const event_engine::event&)> onEngineStart;
    std::function<void(const event_engine::event&)> onEngineStop;
    std::function<void(const event_engine::event&)> onFrame;
    std::function<void(const event_engine::event&)> onRenderScene;
    std::function<void(const event_engine::event&)> onRenderUi;
    std::function<void(const event_engine::event&)> onKeyDown;
    std::function<void(const event_engine::event&)> onKeyUp;
    std::function<void(const event_engine::event&)> onMouseKeyDown;
    std::function<void(const event_engine::event&)> onMouseKeyUp;
    std::function<void(const event_engine::event&)> onMouseMove;

    GameModuleInfo() :
        onEngineStart { nullptr },
        onEngineStop { nullptr },
        onFrame { nullptr },
        onRenderScene { nullptr },
        onRenderUi { nullptr },
        onKeyDown { nullptr },
        onKeyUp { nullptr },
        onMouseKeyDown { nullptr },
        onMouseKeyUp { nullptr },
        onMouseMove { nullptr }
    {}
};

void RegisterGameModule(struct GameModuleInfo &info);

#ifndef INTERNAL_GAMEMODULE_IMPLEMENTATION
#define GAME_MODULE() bool ModuleInit()
static bool ModuleInit();
static bool initStatus = ModuleInit();
#endif
