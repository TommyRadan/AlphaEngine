#include <MediaLayer\SDL\SDL.h>

#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

#include <RenderingEngine\RenderingEngine.hpp>

int main(int argc, char* argv[])
{
	MediaLayer::Display::GetInstance()->Init();
	RenderingEngine::GetInstance()->Init();

	for (;;)
	{
		MediaLayer::Events::GetInstance()->Process();
		if (MediaLayer::Events::GetInstance()->IsQuitRequested()) break;
		
		OpenGL::Context::GetInstance()->ClearColor(Color(52, 152, 219, 255));
		OpenGL::Context::GetInstance()->Clear();
		
		MediaLayer::Display::GetInstance()->SwapBuffers();
	}

	RenderingEngine::GetInstance()->Quit();
	MediaLayer::Display::GetInstance()->Quit();
    return 0;
}