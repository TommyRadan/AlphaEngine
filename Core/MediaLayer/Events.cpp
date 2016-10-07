#include "Events.hpp"

#include "SDL\SDL.h"

void Events::Process(void)
{
	SDL_Event events;

	while (SDL_PollEvent(&events)) {
		switch (events.type) {
			case SDL_MOUSEMOTION: {
				break;
			}
			case SDL_MOUSEWHEEL: break;
			case SDL_MOUSEBUTTONDOWN: break;
			case SDL_MOUSEBUTTONUP: break;
			case SDL_KEYDOWN: {
				switch (events.key.keysym.sym) {
					case KeyBinding::KEY_ESCAPE: m_IsUserQuit = true; break;
				}
				break;
			}
			case SDL_KEYUP: break;
		}
	}
}

void Events::Update(unsigned long deltaTime)
{}