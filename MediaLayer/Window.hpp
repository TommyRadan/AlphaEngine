#pragma once

// #include <GL/Platform.hpp>

#include "Events.hpp"

#include <string>
#include <cstring>
#include <queue>

namespace MediaLayer
{
	enum class WindowStyle
	{
		Base = 0,
		Resize = 1,
		Close = 2,
		Fullscreen = 4
	};

	WindowStyle operator|(const WindowStyle ws1, const WindowStyle ws2)
	{
		return (WindowStyle)(long(ws1) | long(ws2));
	}

	struct Window
	{
		Window(
			const unsigned int width,
			const unsigned int height,
			const std::string& title,
			const WindowStyle style
		);
		~Window(void);
		
		const int GetX(void) const;
		const int GetY(void) const;
		void SetPos(const int x, const int y);

		const unsigned int GetWidth(void) const;
		const unsigned int GetHeight(void) const;
		void SetSize(const unsigned int width, const unsigned int height);

		const bool IsOpen(void) const;
		const bool HasFocus(void) const;

		void SetTitle(const std::string& title);
		void SetVisible(const bool visible);

		void Close(void);

		const bool GetEvent(Event& ev);
		
		const int GetMouseX(void) const;
		const int GetMouseY(void) const;

		const bool IsMouseButtonDown(const MouseButton button) const;
		const bool IsKeyDown(const Key key) const;

		//void* const GetHandle(void) const;
		void Present(void);

	private:
		unsigned int m_Width, m_Height;
		int m_X, m_Y;
		bool m_IsOpen, m_IsFocused;
		std::queue<Event> m_Events;

		unsigned int m_MouseX, m_MouseY;
		bool m_Mouse[3];
		bool m_Keys[100];

#if defined PLATFORM_WINDOWS

		HWND window;
		DWORD style;

		LRESULT WindowEvent(UINT msg, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK WindowEventHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#elif defined PLATFORM_LINUX

		::Window window;
		Display* display;
		Atom close;
		bool fullscreen;
		int screen;
		int oldVideoMode;

		void EnableFullscreen(bool enabled, int width = 0, int height = 0);
		void WindowEvent(const XEvent& event);
		static Bool CheckEvent(Display*, XEvent* event, XPointer userData);

#endif		

		Window(const Window&);
		const Window& operator=(const Window&);

		static Key TranslateKey(const unsigned int code) const;
	};
}
