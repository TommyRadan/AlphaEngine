#pragma once

#include <string>

enum class WinType
{
	WIN_TYPE_WINDOWED,
	WIN_TYPE_BORDERLESS,
	WIN_TYPE_FULLSCREEN
};

class Settings
{
	Settings();

public:
	static Settings* const GetInstance();

	const unsigned int GetWindowWidth() const noexcept;
	const unsigned int GetWindowHeight() const noexcept;
	const char* GetWindowName() const noexcept;
	const WinType GetWindowType() const noexcept;
	const bool IsDoubleBuffered() const noexcept;
	const float GetFieldOfView() const noexcept;
	const float GetMouseSensitivity() const noexcept;
	const float GetAspectRatio() const noexcept;
	const bool IsMouseReversed() const noexcept;

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
