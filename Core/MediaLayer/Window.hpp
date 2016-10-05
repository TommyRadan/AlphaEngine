#pragma once

#include "Event.hpp"

#include <GL/GL/Context.hpp>

#include <string>
#include <cstring>
#include <queue>

enum class WindowStyle
{
	Base = 0,
	Resize = 1,
	Close = 2,
	Fullscreen = 4
};
		
inline WindowStyle operator|(const WindowStyle lft, const WindowStyle rht)
{
	return (WindowStyle)( (int)lft | (int)rht );
}

class Window
{
public:
	Window(
		const unsigned int width, 
		const unsigned int height, 
		const std::string& title, 
		WindowStyle style
	);
	~Window(void);
	
	const int GetX(void) const { return x; }
	const int GetY(void) const { return y; }

	const unsigned int GetWidth(void) const { return width; }
	const unsigned int GetHeight(void) const { return height; }

	bool IsOpen(void) { return open; }
	bool HasFocus(void) { return focus; }

	void SetPos(int x, int y);
	void SetSize(unsigned int width, unsigned int height);

	void SetTitle(const std::string& title);

	void SetVisible(bool visible);

	void Close(void);

	bool GetEvent(Event& ev);
	
	int GetMouseX(void) { return mousex; }
	int GetMouseY(void) { return mousey; }

	bool IsMouseButtonDown(MouseButton button)
	{
		if ( button >= sizeof( mouse ) / sizeof( bool ) ) return false;
		return mouse[button];
	}

	bool IsKeyDown(Key key)
	{
		if ( key >= sizeof( keys ) / sizeof( bool ) ) return false;
		return keys[key];
	}

	Context& GetContext(uchar color = 32, uchar depth = 24, uchar stencil = 8, uchar antialias = 1);
	void Present();

private:
	unsigned int width, height;
	int x, y;
	bool open, focus;
	std::queue<Event> events;

	unsigned int mousex, mousey;
	bool mouse[3];
	bool keys[100];

	Context* context;

	HWND window;
	DWORD style;

	LRESULT WindowEvent(UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK WindowEventHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);		

	Window(const Window&);
	const Window& operator=(const Window&);

	const Key TranslateKey(const unsigned int code) const;
};
