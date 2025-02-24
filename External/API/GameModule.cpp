/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
#include <EventEngine/Dispatch.hpp>

void RegisterGameModule(struct GameModuleInfo &info)
{
    if (info.onEngineStart)
    {
        EventEngine::Dispatch::get_instance().RegisterOnEngineStartCallback(info.onEngineStart);
    }

    if (info.onEngineStop)
    {
        EventEngine::Dispatch::get_instance().RegisterOnEngineStopCallback(info.onEngineStop);
    }

    if (info.onFrame)
    {
        EventEngine::Dispatch::get_instance().RegisterOnFrameCallback(info.onFrame);
    }

    if (info.onRenderScene)
    {
        EventEngine::Dispatch::get_instance().RegisterOnRenderSceneCallback(info.onRenderScene);
    }

    if (info.onRenderUi)
    {
        EventEngine::Dispatch::get_instance().RegisterOnRenderUiCallback(info.onRenderUi);
    }

    if (info.onKeyDown)
    {
        EventEngine::Dispatch::get_instance().RegisterOnKeyDownCallback(info.onKeyDown);
    }

    if (info.onKeyUp)
    {
        EventEngine::Dispatch::get_instance().RegisterOnKeyUpCallback(info.onKeyUp);
    }

    if (info.onMouseMove)
    {
        EventEngine::Dispatch::get_instance().RegisterOnMouseMoveCallback(info.onMouseMove);
    }
}
