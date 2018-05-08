#include <EventEngine/EventEngine.hpp>
#include <SDL2/SDL.h>
#include <Infrastructure/Exception.hpp>

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
		throw Exception("EventEngine second initialization attempt");
	}

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        throw Exception("Could not initialize event system");
    }

    m_IsInitialized = true;
}

void EventEngine::Context::Quit()
{
	if (!m_IsInitialized)
    {
        throw Exception("EventEngine quit attempt without initialization");
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);

    m_IsInitialized = false;
}

void EventEngine::Context::RequestQuit()
{
    m_IsQuitRequested = true;
}

const bool EventEngine::Context::IsQuitRequested() const
{
    return m_IsQuitRequested;
}
