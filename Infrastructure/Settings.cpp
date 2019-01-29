/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Infrastructure/Settings.hpp>
#include <SDL.h>
#include <Infrastructure/Version.hpp>

Settings::Settings()
{
#if _DEBUG
	m_WindowWidth = 800;
	m_WindowHeight = 600;
	m_WindowType = WinType::WIN_TYPE_WINDOWED;
#else
	SDL_DisplayMode displayMode;
	SDL_GetCurrentDisplayMode(0, &displayMode);

	m_WindowWidth = displayMode.w;
	m_WindowHeight = displayMode.h;
	m_WindowType = WinType::WIN_TYPE_FULLSCREEN;
#endif

    m_WindowName = std::string{ "AlphaEngine v" } + Infrastructure::Version::GetVersion();
    m_IsDoubleBuffered = true;
    m_FieldOfView = 70.0f;
    m_MouseSensitivity = 0.005f;
    m_IsMouseReversed = false;
}

Settings* const Settings::GetInstance()
{
    static Settings* instance = nullptr;

    if (instance == nullptr)
    {
        instance = new Settings();
    }

    return instance;
}

const unsigned int Settings::GetWindowWidth() const noexcept
{
    return m_WindowWidth;
}

const unsigned int Settings::GetWindowHeight() const noexcept
{
    return m_WindowHeight;
}

const char* Settings::GetWindowName() const noexcept
{
    return m_WindowName.c_str();
}

const WinType Settings::GetWindowType() const noexcept
{
    return m_WindowType;
}

const bool Settings::IsDoubleBuffered() const noexcept
{
    return m_IsDoubleBuffered;
}

const float Settings::GetFieldOfView() const noexcept
{
    return m_FieldOfView;
}

const float Settings::GetMouseSensitivity() const noexcept
{
    return m_MouseSensitivity;
}

const float Settings::GetAspectRatio() const noexcept
{
    return float(m_WindowWidth) / m_WindowHeight;
}

const bool Settings::IsMouseReversed() const noexcept
{
    return m_IsMouseReversed;
}
