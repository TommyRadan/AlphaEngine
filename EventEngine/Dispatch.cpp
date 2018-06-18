/**
 * Copyright (c) 2018 Tomislav Radanovic
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

#include <EventEngine/Dispatch.hpp>
#include <EventEngine/EventEngine.hpp>
#include <SDL2/SDL.h>
#include <algorithm>

EventEngine::Dispatch* EventEngine::Dispatch::GetInstance()
{
    static Dispatch* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Dispatch;
    }

    return instance;
}

void EventEngine::Dispatch::HandleEvents()
{
    SDL_Event events = { 0 };

    while (SDL_PollEvent(&events))
    {
        switch (events.type)
        {
            case SDL_KEYDOWN:
                DispatchOnKeyDownCallback((KeyCode) events.key.keysym.sym);
                break;

            case SDL_KEYUP:
                DispatchOnKeyUpCallback((KeyCode) events.key.keysym.sym);
                break;

            case SDL_MOUSEBUTTONDOWN:
                switch(events.button.button)
                {
                    case SDL_BUTTON_LEFT:
                        DispatchOnKeyDownCallback(KeyCode::MOUSE_LEFT);
                        break;

                    case SDL_BUTTON_RIGHT:
                        DispatchOnKeyDownCallback(KeyCode::MOUSE_RIGHT);
                        break;

                    case SDL_BUTTON_MIDDLE:
                        DispatchOnKeyDownCallback(KeyCode::MOUSE_MIDDLE);
                        break;

                    default:
                        break;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                switch(events.button.button)
                {
                    case SDL_BUTTON_LEFT:
                        DispatchOnKeyUpCallback(KeyCode::MOUSE_LEFT);
                        break;

                    case SDL_BUTTON_RIGHT:
                        DispatchOnKeyUpCallback(KeyCode::MOUSE_RIGHT);
                        break;

                    case SDL_BUTTON_MIDDLE:
                        DispatchOnKeyUpCallback(KeyCode::MOUSE_MIDDLE);
                        break;

                    default:
                        break;
                }
                break;

            case SDL_MOUSEMOTION:
                DispatchOnMouseMoveCallback(events.motion.xrel, events.motion.yrel);
                break;

            case SDL_QUIT:
                EventEngine::Context::GetInstance()->RequestQuit();
                break;

            default:
                break;
        }
    }

    HandleFrame();
}

void EventEngine::Dispatch::HandleFrame()
{
    const uint32_t ticks = SDL_GetTicks();

    static uint32_t lastTickCount = ticks;
    uint32_t deltaTime = ticks - lastTickCount;
    lastTickCount = ticks;

    // First Update will be 0, so we ignore it
    if (deltaTime == 0u)
    {
        return;
    }

    for (auto& keyCode : m_PressedKeys)
    {
        DispatchKeyPressedCallback(keyCode, deltaTime);
    }

    DispatchOnFrameCallback(deltaTime);
}

void EventEngine::Dispatch::RegisterOnGameStartCallback(std::function<void()> callback)
{
	m_OnGameStartCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterOnGameEndCallback(std::function<void()> callback)
{
	m_OnGameEndCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterOnFrameCallback(std::function<void(uint32_t)> callback)
{
    m_OnFrameCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterOnKeyDownCallback(std::function<void(KeyCode)> callback)
{
    m_OnKeyDownCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterOnKeyUpCallback(std::function<void(KeyCode)> callback)
{
    m_OnKeyUpCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterKeyPressedCallback(std::function<void(KeyCode, uint32_t)> callback)
{
    m_KeyPressedCallbacks.push_back(callback);
}

void EventEngine::Dispatch::RegisterOnMouseMoveCallback(std::function<void(int32_t, int32_t)> callback)
{
    m_OnMouseMoveCallbacks.push_back(callback);
}

void EventEngine::Dispatch::DispatchOnGameStartCallback()
{
	for (auto& callback : m_OnGameStartCallbacks)
	{
		callback();
	}
}

void EventEngine::Dispatch::DispatchOnGameEndCallback()
{
	for (auto& callback : m_OnGameEndCallbacks)
	{
		callback();
	}
}

void EventEngine::Dispatch::DispatchOnFrameCallback(uint32_t deltaTime)
{
    for (auto& callback : m_OnFrameCallbacks)
    {
        callback(deltaTime);
    }
}

void EventEngine::Dispatch::DispatchOnKeyDownCallback(KeyCode keyCode)
{
    m_PressedKeys.erase(std::remove(m_PressedKeys.begin(), m_PressedKeys.end(), keyCode), m_PressedKeys.end());
    m_PressedKeys.push_back(keyCode);

    for (auto& callback : m_OnKeyDownCallbacks)
    {
        callback(keyCode);
    }
}

void EventEngine::Dispatch::DispatchOnKeyUpCallback(KeyCode keyCode)
{
    m_PressedKeys.erase(std::remove(m_PressedKeys.begin(), m_PressedKeys.end(), keyCode), m_PressedKeys.end());

    for (auto& callback : m_OnKeyUpCallbacks)
    {
        callback(keyCode);
    }
}

void EventEngine::Dispatch::DispatchKeyPressedCallback(KeyCode keyCode, uint32_t deltaTime)
{
    for (auto& callback : m_KeyPressedCallbacks)
    {
        callback(keyCode, deltaTime);
    }
}

void EventEngine::Dispatch::DispatchOnMouseMoveCallback(int32_t deltaX, int32_t deltaY)
{
    for (auto& callback : m_OnMouseMoveCallbacks)
    {
        callback(deltaX, deltaY);
    }
}

bool EventEngine::Dispatch::IsKeyPressed(KeyCode keyCode)
{
    auto position = std::find(m_PressedKeys.begin(), m_PressedKeys.end(), keyCode);

    return (position != m_PressedKeys.end());
}
