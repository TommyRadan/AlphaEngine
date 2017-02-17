#pragma once

#include <Utilities/Component.hpp>
#include <Utilities/Exception.hpp>

namespace RenderingEngine
{
	struct Context :
			public Component
	{
		static Context *GetInstance(void) {
			static Context *instance = nullptr;
			if (instance == nullptr) {
				instance = new Context();
			}
			return instance;
		}

		void Init(void) override;
		void Quit(void) override;

	private:
		Context(void);
	};
}
