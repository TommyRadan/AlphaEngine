#pragma once

#include <Utilities\Singleton.hpp>

#include<string>

enum class WinType {
	WIN_TYPE_WINDOWED,
	WIN_TYPE_BORDERLESS,
	WIN_TYPE_FULLSCREEN
};

class Settings : Singleton<Settings>
{
	friend Singleton<Settings>;
	Settings(void) {
		m_WindowWidth = 1920;
		m_WindowHeight = 1080;
		m_WindowName = "Hello World!";
		m_WindowType = WinType::WIN_TYPE_FULLSCREEN;
		m_IsDoubleBuffered = true;
		m_FieldOfView = 70.0f;
		m_MouseSensitivity = 0.005f;
		m_IsMouseReversed = true;
	}

public:
	const unsigned int GetWindowWidth(void) const noexcept { return m_WindowWidth; }
	const unsigned int GetWindowHeight(void) const noexcept { return m_WindowHeight; }
	const char* GetWindowName(void) const noexcept { return m_WindowName.c_str(); }
	const WinType GetWindowType(void) const noexcept { return m_WindowType; }
	const bool IsDoubleBuffered(void) const noexcept { return m_IsDoubleBuffered; }
	const float GetFieldOfView(void) const noexcept { return m_FieldOfView; }
	const float GetMouseSensitivity(void) const noexcept { return m_MouseSensitivity; }
	const float GetAspectRatio(void) const noexcept { return float(m_WindowWidth) / m_WindowHeight; }
	const bool IsMouseReversed(void) const noexcept { return m_IsMouseReversed; }

private:
	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;
	std::string m_WindowName;
	WinType m_WindowType;
	bool m_IsDoubleBuffered;
	float m_FieldOfView;
	float m_MouseSensitivity;
	bool m_IsMouseReversed;
};
