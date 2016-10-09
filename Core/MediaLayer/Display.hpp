#pragma once

// Singleton
#include <Utilities\Singleton.hpp>

// Exception
#include <Utilities\Exception.hpp>

// Global settings
#include <Control\Settings.hpp>

namespace MediaLayer
{
	struct Display : public Singleton<Display>
	{
		void Init(void);
		void Quit(void);

		void SwapBuffers(void);

	private:
		friend Singleton<Display>;
		Display(void);

		bool m_IsInit;

		void* m_Window;
		void* m_GlContext;

		Settings* m_Settings;
	};
}
