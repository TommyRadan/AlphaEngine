#pragma once

#include <Utilities\Singleton.hpp>

class EventCallBacks : public Singleton<EventCallBacks>
{
	friend Singleton<EventCallBacks>;

public:
	void(*OnEngineStarted)();
	void(*OnEngineStopped)();
	void(*OnFrame)(float);
	void(*OnKeyDown)(int);
	void(*OnKeyUp)(int);
	void(*OnMouseMove)(int, int);
};
