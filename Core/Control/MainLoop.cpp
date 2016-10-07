#include <MediaLayer\SDL\SDL.h>

#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

int main(int argc, char* argv[])
{
	Display* display = new Display();
	Events* events = new Events();

	for (;;)
	{
		events->Process();
		if (events->IsQuitRequested()) break;


		
		display->SwapBuffers();
	}

	delete events;
	delete display;
    return 0;
}