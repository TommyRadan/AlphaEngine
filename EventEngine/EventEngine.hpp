#pragma once

#include <Infrastructure/Subsystem.hpp>

namespace EventEngine
{
	class Context : public Infrastructure::Subsystem
	{
		Context();

	public:
		static Context* GetInstance();

		void Init() final;
		void Quit() final;

		void RequestQuit();
		const bool IsQuitRequested() const;

	private:
		bool m_IsQuitRequested;
	};
}
