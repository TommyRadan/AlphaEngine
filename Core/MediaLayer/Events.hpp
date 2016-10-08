#pragma once

#include <Utilities\Singleton.hpp>

enum KeyBinding {
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

struct Events : public Singleton<Events>
{
	void Process(void);
	void Update(void);
	bool IsQuitRequested(void) const;

private:
	friend Singleton<Events>;
	Events(void) :
		m_IsUserQuit{ false }
	{}

	bool m_IsUserQuit;
};