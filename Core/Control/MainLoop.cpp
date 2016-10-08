#include <MediaLayer\SDL\SDL.h>

#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

int main(int argc, char* argv[])
{
	Display::GetInstance()->Init();

	for (;;)
	{
		Events::GetInstance()->Process();
		if (Events::GetInstance()->IsQuitRequested()) break;


		
		Display::GetInstance()->SwapBuffers();
	}

	Display::GetInstance()->Quit();
    return 0;
}