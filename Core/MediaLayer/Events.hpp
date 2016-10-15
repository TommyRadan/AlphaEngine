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
		KEY_I = 105,
		KEY_O = 111,
		KEY_P = 112,
		KEY_Š = 353,
		KEY_Ð = 273,
		KEY_Ž = 382,
		KEY_F = 102,
		KEY_G = 103,
		KEY_H = 104,
		KEY_J = 106,
		KEY_K = 107,
		KEY_L = 108,
		KEY_È = 269,
		KEY_Æ = 263,
		KEY_Y = 121,
		KEY_X = 120,
		KEY_C = 99,
		KEY_V = 118,
		KEY_B = 98,
		KEY_N = 110,
		KEY_M = 109,
		KEY_SPACE = 32,
		KEY_LSHIFT = 1073742049,
		KEY_RSHIFT = 1073742053,
		KEY_LCTRL = 1073742048,
		KEY_RCTRL = 1073742052,
		KEY_LALT = 1073742050,
		KEY_RALT = 1073742048,
		KEY_ENTER = 13,
		KEY_BACKSPACE = 8,
		KEY_ESCAPE = 27,
		KEY_TAB = 9,
		KEY_DEL = 127,
		KEY_INSERT = 1073741897,
		KEY_END = 1073741901,
		KEY_HOME = 1073741901,
		KEY_PGUP = 1073741899,
		KEY_PGOWN = 1073741902,
		KEY_COMMA = 44,
		KEY_PERIOD = 46,
		KEY_UP = 1073741906,
		KEY_DOWN = 1073741905,
		KEY_LEFT = 1073741904,
		KEY_RIGHT = 1073741903,
		KEY_1 = 49,
		KEY_2 = 50,
		KEY_3 = 51,
		KEY_4 = 52,
		KEY_5 = 53,
		KEY_6 = 54,
		KEY_7 = 55,
		KEY_8 = 56,
		KEY_9 = 57,
		KEY_0 = 48,
		KEY_F1 = 1073741882,
		KEY_F2 = 1073741883,
		KEY_F3 = 1073741884,
		KEY_F4 = 1073741885,
		KEY_F5 = 1073741886,
		KEY_F6 = 1073741887,
		KEY_F7 = 1073741888,
		KEY_F8 = 1073741889,
		KEY_F9 = 1073741890,
		KEY_F10 = 1073741891,
		KEY_F11 = 1073741892,
		KEY_F12 = 1073741893
		
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
