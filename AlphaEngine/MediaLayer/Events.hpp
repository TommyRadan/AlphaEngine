#pragma once

// SDL
#include <SDL.h>

#include "../Utilities/Singleton.hpp"

#include <list>
#include <functional>

enum class KeyBinding
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
	Events(void) { }

public:
	void Process(void);

	bool IsQuitRequested(void) const  {
		if (m_Events.type == SDL_QUIT) return true;
		return false;
	}

	void AddKeyboardEventCallback(std::function<void(int)> func)
	{
		m_KeyboardCallbacks.push_back(func);
	}

	void RemoveKeyboardEventCallback(std::function<void(int)> func)
	{
		m_KeyboardCallbacks.remove(func);
	}

private:
	SDL_Event m_Events;

	std::list<std::function<void(int)>> m_KeyboardCallbacks;
};
