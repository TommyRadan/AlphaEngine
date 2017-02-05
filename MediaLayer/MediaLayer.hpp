#pragma once

#include <Utilities/Singleton.hpp>
#include <Utilities/Exception.hpp>
#include <Utilities/Component.hpp>

// Global settings
#include <Control/Settings.hpp>

// Standard Library
#include <string>

struct MediaLayer : 
	public Singleton<MediaLayer>,
	public Component
{
	void Init(void) override;
	void Quit(void) override;

	void SwapBuffers(void);
	void ShowDialog(const std::string&, const std::string&);

private:
	template<typename MediaLayer>
	friend class Singleton;
	MediaLayer(void);

	const Settings* const m_Settings;
};
