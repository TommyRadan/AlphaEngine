#pragma once

#include <Utilities/Exception.hpp>
#include <Utilities/Component.hpp>

// Global settings
#include <Control/Settings.hpp>

// Standard Library
#include <string>

struct MediaLayer :
	public Component
{
	static MediaLayer* GetInstance(void)
	{
		static MediaLayer* instance = nullptr;
		if(instance == nullptr) {
			instance = new MediaLayer();
		}
		return instance;
	}

	void Init(void) override;
	void Quit(void) override;

	void SwapBuffers(void);
	void ShowDialog(const std::string&, const std::string&);

private:
	MediaLayer(void);

	const Settings* const m_Settings;
};
