#include <MediaLayer/MediaLayer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>

int main(int argc, char* argv[])
{
	(void) argc;
	(void) argv;

	try {
		MediaLayer::Context::GetInstance()->Init();
		RenderingEngine::Context::GetInstance()->Init();
	} catch (Exception e) {
		MediaLayer::Context::GetInstance()->ShowDialog("Initialization error!", e.what());
		return 1;
	}

	try {
		for (;;)
		{
			MediaLayer::Events::GetInstance()->Process();
            MediaLayer::Events::GetInstance()->Update();
            if(MediaLayer::Events::GetInstance()->IsQuitRequested()) break;

            // Scene should be rendered here

            MediaLayer::Window::GetInstance()->SwapBuffers();
		}
	} catch (Exception e) {
		MediaLayer::Context::GetInstance()->ShowDialog("Runtime error!", e.what());
		return 1;
	}

	RenderingEngine::Context::GetInstance()->Quit();
	MediaLayer::Context::GetInstance()->Quit();
    return 0;
}
