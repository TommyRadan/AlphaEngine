#include <MediaLayer\SDL\SDL.h>

#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

#include <RenderingEngine\OpenGL\OpenGL.hpp>

int main(int argc, char* argv[])
{
	MediaLayer::Display::GetInstance()->Init();
	OpenGL::Context::GetInstance()->Init();

	for (;;)
	{
		MediaLayer::Events::GetInstance()->Process();
		if (MediaLayer::Events::GetInstance()->IsQuitRequested()) break;

		OpenGL::Context::GetInstance()->ClearColor(Color(41, 128, 185, 255));
		OpenGL::Context::GetInstance()->Clear();
		
		MediaLayer::Display::GetInstance()->SwapBuffers();
	}

	OpenGL::Context::GetInstance()->Quit();
	MediaLayer::Display::GetInstance()->Quit();
    return 0;
}