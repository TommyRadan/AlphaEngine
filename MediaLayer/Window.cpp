#include "Window.hpp"

namespace MediaLayer
{
	const int Window::GetX(void) const
	{
		return m_X;
	}

	const int Window::GetY(void) const
	{
		return m_Y;
	}

	const unsigned int Window::GetWidth(void) const
	{
		return m_Width;
	}

	const unsigned int Window::GetHeight(void) const
	{
		return m_Height;
	}

	const bool Window::IsOpen(void) const
	{
		return m_IsOpen;
	}

	const bool Window::HasFocus(void) const
	{
		return m_IsInFocus;
	}

	const int Window::GetMouseX(void) const
	{
		return m_MouseX;
	}

	const int Window::GetMouseY(void) const
	{
		return m_MouseY;
	}

	const bool Window::IsMouseButtonDown(const MouseButton button) const
	{
		if(button >= sizeof(m_Mouse) / sizeof(bool)) return false;
		return m_Mouse[button];
	}

	const bool Window::IsKeyDown(const Key key) const
	{
		if(key >= sizeof(m_Keys) / sizeof(bool)) return false;
		return m_Keys[key];
	}
}