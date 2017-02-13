#pragma once

#include <string>

enum class WinType {
	WIN_TYPE_WINDOWED,
	WIN_TYPE_BORDERLESS,
	WIN_TYPE_FULLSCREEN
};

class Settings
{
	Settings(void) {
#ifdef _DEBUG
		m_WindowWidth = 640;
		m_WindowHeight = 480;
		m_WindowType = WinType::WIN_TYPE_WINDOWED;
#else
		m_WindowWidth = 1920;
		m_WindowHeight = 1200;
		m_WindowType = WinType::WIN_TYPE_FULLSCREEN;
#endif

		m_WindowName = "Alpha Engine";
		m_IsDoubleBuffered = true;
		m_FieldOfView = 70.0f;
		m_MouseSensitivity = 0.005f;
		m_IsMouseReversed = true;
	}

public:
	static Settings* const GetInstance(void)
	{
		static Settings* instance = nullptr;
		if(instance == nullptr) {
			instance = new Settings();
		}
		return instance;
	}

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
