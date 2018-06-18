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

#include <EventEngine/EventEngine.hpp>
#include <SDL2/SDL.h>
#include <Infrastructure/Exception.hpp>
#include <Infrastructure/Log.hpp>

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
	if (m_IsInitialized)
	{
	    LOG_WARN("EventEngine second initialization attempt");
		return;
	}

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        LOG_FATAL("Could not initialize event system");
        LOG_FATAL("SDL_Error: %s", SDL_GetError());
        throw Exception("Could not initialize event system");
    }

    m_IsInitialized = true;
}

void EventEngine::Context::Quit()
{
	if (!m_IsInitialized)
    {
        LOG_WARN("EventEngine quit attempt without initialization");
        return;
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);

    m_IsInitialized = false;
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
