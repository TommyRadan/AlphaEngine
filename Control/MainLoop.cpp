#include <MediaLayer\Display.hpp>
#include <MediaLayer\Events.hpp>

#include <RenderingEngine\RenderingEngine.hpp>

int main(int argc, char* argv[])
{
	try {
		MediaLayer::Display::GetInstance()->Init();
		RenderingEngine::GetInstance()->Init();
	} catch (Exception e) {
		MediaLayer::Display::GetInstance()->ShowDialog("Initialization error!", e.what());
		return 1;
	}

	try {
		for (;;)
		{
			MediaLayer::Events::GetInstance()->Process();
			if (MediaLayer::Events::GetInstance()->IsQuitRequested()) break;

			RenderingEngine::GetInstance()->RenderScene();
			MediaLayer::Display::GetInstance()->SwapBuffers();
		}
	} catch (Exception e) {
		MediaLayer::Display::GetInstance()->ShowDialog("Runtime error!", e.what());
		return 1;
	}

	RenderingEngine::GetInstance()->Quit();
	MediaLayer::Display::GetInstance()->Quit();
    return 0;
}