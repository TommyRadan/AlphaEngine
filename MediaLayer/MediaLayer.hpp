#pragma once

#include <Utilities/ISingleton.hpp>
#include <Utilities/Exception.hpp>
#include <Utilities/IComponent.hpp>

// Global settings
#include <Control/Settings.hpp>

struct MediaLayer : 
	public ISingleton<MediaLayer>,
	public IComponent
{
	void Init(void) override;
	void Quit(void) override;

	void SwapBuffers(void);
	void ShowDialog(const char* title, const char* text);

private:
	template<typename MediaLayer>
	friend class ISingleton;
	MediaLayer(void);

	const Settings* const m_Settings;
};
