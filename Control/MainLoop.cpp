#include <cstdlib>

#include <EventEngine/EventEngine.hpp>
#include <Infrastructure/Exception.hpp>
#include <EventEngine/Dispatch.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Window.hpp>
#include <Infrastructure/Log.hpp>

int main(int argc, char* argv[])
{
    LOG_INIT(argc, argv);

    try
    {
        Infrastructure::Log::GetInstance()->Init();
        EventEngine::Context::GetInstance()->Init();
        RenderingEngine::Context::GetInstance()->Init();
    }
    catch (const Exception& e)
    {
        RenderingEngine::Window::GetInstance()->ShowMessage("Initialization Error", e.what());
        return EXIT_FAILURE;
    }

	EventEngine::Dispatch::GetInstance()->DispatchOnGameStartCallback();

    try
    {
        for (;;)
        {
            EventEngine::Dispatch::GetInstance()->HandleEvents();
            RenderingEngine::Context::GetInstance()->Render();

            if (EventEngine::Context::GetInstance()->IsQuitRequested()) break;
        }
    }
    catch (const Exception& e)
    {
        RenderingEngine::Window::GetInstance()->ShowMessage("Error", e.what());
        return EXIT_FAILURE;
    }

	EventEngine::Dispatch::GetInstance()->DispatchOnGameEndCallback();

    RenderingEngine::Context::GetInstance()->Quit();
    EventEngine::Context::GetInstance()->Quit();
    return EXIT_SUCCESS;
}
