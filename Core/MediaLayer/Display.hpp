#pragma once

// Exception
#include <Utilities\Exceptions\Exception.hpp>

// Global settings
#include <Control\Settings.hpp>

struct Display
{
	Display(void);
	~Display(void);

	void SwapBuffers(void);

private:
	void* m_Window;
	void* m_GlContext;

	Settings* m_Settings;
};
