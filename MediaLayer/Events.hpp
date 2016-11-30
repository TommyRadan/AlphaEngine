#pragma once

#include <Utilities\Singleton.hpp>

namespace MediaLayer
{
	enum class MouseButton
	{
		Left,
		Right,
		Middle
	};

	struct alignas(16) MouseEvent
	{
		unsigned int X, Y;
		union
		{
			int Delta;
			MouseButton Button;
		};
	};

	struct alignas(16) KeyEvent
	{
		Key Code;
		bool Alt;
		bool Control;
		bool Shift;
	};

	struct alignas(16) WindowEvent
	{
		union { int X; unsigned int Width; };
		union { int Y; unsigned int Height; };
	};

	struct Event
	{
		enum event_t
		{
			Unknown,
			Close,
			Resize,
			Move,
			Focus,
			Blur,
			KeyDown,
			KeyUp,
			MouseDown,
			MouseUp,
			MouseWheel,
			MouseMove
		};

		event_t Type;

		union
		{
			MouseEvent Mouse;
			KeyEvent Key;
			WindowEvent Window;
		};
	};

	enum class Key
	{
		Unknown,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		A, B, C, D, E, F, G, H, I, J, K, L,	M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		LeftBracket, RightBracket, Semicolon, Comma, Period, Quote, Slash, Backslash, Tilde, Equals, Hyphen,
		Escape, Control, Shift, Alt, Space, Enter, Backspace, Tab, PageUp, PageDown, End, Home, Insert, Delete, Pause,
		Left, Right, Up, Down,
		Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
		Add, Subtract, Multiply, Divide
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
