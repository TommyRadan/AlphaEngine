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

#include <exception>

#include <EventEngine/EventEngine.hpp>
#include <EventEngine/Dispatch.hpp>
#include <SDL.h>
#include <Infrastructure/Log.hpp>
#include <stdexcept>

EventEngine::Context::Context() :
    m_IsQuitRequested { false }
{}

EventEngine::Context* EventEngine::Context::GetInstance()
{
    static Context* instance { nullptr };

    if (instance == nullptr)
    {
        instance = new Context();
    }

    return instance;
}

void EventEngine::Context::Init()
{
	LOG_INFO("Init Event Engine");

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        LOG_FATAL("Could not initialize event system");
        LOG_FATAL("SDL_Error: %s", SDL_GetError());
        throw std::runtime_error {"Could not initialize event system"};
    }
}

void EventEngine::Context::Quit()
{
	SDL_QuitSubSystem(SDL_INIT_VIDEO);

    LOG_INFO("Quit Event Engine");
}

void EventEngine::Context::RequestQuit()
{
    LOG_INFO("Quit has been requested");
    m_IsQuitRequested = true;
}

const bool EventEngine::Context::IsQuitRequested() const
{
    return m_IsQuitRequested;
}

void EventEngine::Context::HandleEvents()
{
    SDL_Event events = { 0 };
    Dispatch *dispatch = Dispatch::GetInstance();

    dispatch->DispatchOnFrameCallback();

    SDL_PumpEvents();
    while (SDL_PollEvent(&events))
    {
        switch (events.type)
        {
        case SDL_KEYDOWN:
            dispatch->DispatchOnKeyDownCallback((KeyCode)events.key.keysym.sym);
            break;

        case SDL_KEYUP:
            dispatch->DispatchOnKeyUpCallback((KeyCode)events.key.keysym.sym);
            break;

        case SDL_MOUSEBUTTONDOWN:
            switch (events.button.button)
            {
            case SDL_BUTTON_LEFT:
                dispatch->DispatchOnKeyDownCallback(KeyCode::MOUSE_LEFT);
                break;

            case SDL_BUTTON_RIGHT:
                dispatch->DispatchOnKeyDownCallback(KeyCode::MOUSE_RIGHT);
                break;

            case SDL_BUTTON_MIDDLE:
                dispatch->DispatchOnKeyDownCallback(KeyCode::MOUSE_MIDDLE);
                break;

            default:
                break;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            switch (events.button.button)
            {
            case SDL_BUTTON_LEFT:
                dispatch->DispatchOnKeyUpCallback(KeyCode::MOUSE_LEFT);
                break;

            case SDL_BUTTON_RIGHT:
                dispatch->DispatchOnKeyUpCallback(KeyCode::MOUSE_RIGHT);
                break;

            case SDL_BUTTON_MIDDLE:
                dispatch->DispatchOnKeyUpCallback(KeyCode::MOUSE_MIDDLE);
                break;

            default:
                break;
            }
            break;

        case SDL_MOUSEMOTION:
            dispatch->DispatchOnMouseMoveCallback(events.motion.xrel, events.motion.yrel);
            break;

        case SDL_QUIT:
            this->m_IsQuitRequested = true;
            break;

        default:
            break;
        }
    }
}
