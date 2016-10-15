#pragma once

#include <Utilities\Singleton.hpp>

namespace MediaLayer
{
	enum KeyBinding {
		KEY_W = 119,
		KEY_A = 97,
		KEY_S = 115,
		KEY_D = 100,
		KEY_Q = 113,
		KEY_E = 101,
		KEY_R = 114,
		KEY_T = 116,
		KEY_Z = 122,
		KEY_U = 117,
		KEY_F = 102,
		KEY_G = 103,
		KEY_H = 104,
		KEY_J = 106,
		KEY_Y = 121,
		KEY_X = 120,
		KEY_C = 99,
		KEY_V = 118,
		KEY_B = 98,
		KEY_N = 110,
		KEY_M = 109,
		KEY_SPACE = 32,
		KEY_SHIFT = 1073742049,
		KEY_CTRL = 1073741881,
		KEY_ENTER = 13,
		KEY_ESCAPE = 27,
		KEY_TAB = 9,
		KEY_1 = 49,
		KEY_2 = 50,
		KEY_3 = 51,
		KEY_4 = 52,
		KEY_5 = 53,
		KEY_6 = 54,
		KEY_7 = 55,
		KEY_8 = 56,
		KEY_9 = 57,
		KEY_0 = 48
		
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
}
