#include <MediaLayer/MediaLayer.hpp>
#include <RenderingEngine/RenderingEngine.hpp>

#include <Utilities/Exception.hpp>

int main(int argc, char* argv[])
{
	(void) argc;
	(void) argv;

	try {
		MediaLayer::GetInstance()->Init();
		RenderingEngine::GetInstance()->Init();
	} catch (Exception e) {
		MediaLayer::GetInstance()->ShowDialog("Initialization error!", e.what());
		return 1;
	}

	try {
		for (;;)
		{
			break; // Exit immediately
		}
	} catch (Exception e) {
		//MediaLayer::Display::GetInstance()->ShowDialog("Runtime error!", e.what());
		return 1;
	}

	RenderingEngine::GetInstance()->Quit();
	MediaLayer::GetInstance()->Quit();
    return 0;
}