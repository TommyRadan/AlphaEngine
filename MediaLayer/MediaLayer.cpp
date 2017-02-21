#include "MediaLayer.hpp"
#include <SDL2/SDL.h>
#include "Window.hpp"

MediaLayer::Context::Context(void) :
	m_Settings{ Settings::GetInstance() }
{
	m_IsInit = false;
}

void MediaLayer::Context::Init(void)
{
	if (m_IsInit) {
		throw Exception("Called MediaLayer::Init twice");
	}

	if(SDL_Init(SDL_INIT_EVERYTHING) == SDL_TRUE) {
        throw Exception("SDL failed to initialize");
    }

    MediaLayer::Window::GetInstance()->Init();

	m_IsInit = true;
	return;
}

void MediaLayer::Context::Quit(void)
{
	if (!m_IsInit) {
		throw Exception("Called MediaLayer::Quit before Display::Init");
	}

    MediaLayer::Window::GetInstance()->Quit();

	SDL_Quit();

	m_IsInit = false;
	return;
}

void MediaLayer::Context::ShowDialog(const std::string& title, const std::string& text)
{
	if (!m_IsInit) {
		throw Exception("Called MediaLayer::ShowDialog before Display::Init");
	}

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), NULL);
}
