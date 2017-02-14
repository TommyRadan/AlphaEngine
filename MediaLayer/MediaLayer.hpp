#pragma once

#include <Utilities/Exception.hpp>
#include <Utilities/Component.hpp>
#include <Control/Settings.hpp>

#include "Window.hpp"
#include "Events.hpp"

#include <string>

namespace MediaLayer {
	struct Context :
			public Component
    {
		static Context *GetInstance(void)
        {
			static Context *instance = nullptr;
			if (instance == nullptr) {
				instance = new Context();
			}
			return instance;
		}

		void Init(void) override;
		void Quit(void) override;

		void ShowDialog(const std::string&, const std::string&);

	private:
		Context(void);

		const Settings* const m_Settings;
	};
}
