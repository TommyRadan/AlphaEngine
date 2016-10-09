#include <MediaLayer\SDL\SDL.h>

#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

#include <RenderingEngine\OpenGL\OpenGL.hpp>

int main(int argc, char* argv[])
{
	MediaLayer::Display::GetInstance()->Init();
	OpenGL::OGL::GetInstance()->Init();

	for (;;)
	{
		MediaLayer::Events::GetInstance()->Process();
		if (MediaLayer::Events::GetInstance()->IsQuitRequested()) break;

		OpenGL::OGL::GetInstance()->ClearColor(Color(41, 128, 185, 255));
		OpenGL::OGL::GetInstance()->Clear();
		
		MediaLayer::Display::GetInstance()->SwapBuffers();
	}

	OpenGL::OGL::GetInstance()->Quit();
	MediaLayer::Display::GetInstance()->Quit();
    return 0;
}