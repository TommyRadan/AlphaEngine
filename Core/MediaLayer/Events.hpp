#pragma once

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

struct Events
{
	Events(void) :
		m_IsUserQuit{ false }
	{}

	void Process(void);
	void Update(unsigned long);
	bool IsQuitRequested(void) const;

private:
	bool m_IsUserQuit;
};