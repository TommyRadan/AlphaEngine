#pragma once

#include <Utilities/Singleton.hpp>
#include <Utilities/Exception.hpp>
#include <Utilities/Component.hpp>

// Global settings
#include <Control/Settings.hpp>

struct MediaLayer : 
	public Singleton<MediaLayer>,
	public Component
{
	void Init(void) override;
	void Quit(void) override;

	void SwapBuffers(void);
	void ShowDialog(const char* title, const char* text);

private:
	friend Singleton<MediaLayer>;
	MediaLayer(void);

	void* m_Window;
	void* m_GlContext;

	Settings* m_Settings;
};
