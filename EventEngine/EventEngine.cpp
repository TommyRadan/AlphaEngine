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
