#pragma once

// SDL
#include <SDL.h>

#include <Utilities\Singleton.hpp>
#include "EventCallbacks.hpp"

enum KeyBinding 
{
	KEY_W = 119,
	KEY_A = 97,
	KEY_S = 115,
	KEY_D = 100,
	KEY_SPACE = 32,
	KEY_SHIFT = 1073742049,
	KEY_CTRL = 1073741881,
	KEY_ENTER = 13,
	KEY_ESCAPE = 27
};

class Events : public Singleton<Events>
{
	friend Singleton<Events>;
	Events(void) : 
		m_IsUserQuit{ false } 
	{ }

public:
	void Process(void);
	void Update(const Uint32);

	bool IsQuitRequested(void) const  {
		if (m_Events.type == SDL_QUIT || m_IsUserQuit) return true;
		return false;
	}

	void RequestQuit(void) {
		m_IsUserQuit = true;
	}

private:
	bool m_IsUserQuit;
	SDL_Event m_Events;
};
