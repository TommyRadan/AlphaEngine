/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <exception>

#include <EventEngine/EventEngine.hpp>
#include <EventEngine/Dispatch.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Window.hpp>
#include <Infrastructure/Log.hpp>
#include <Infrastructure/Time.hpp>
#include <SceneGraph/SceneGraph.hpp>

int main(int argc, char* argv[])
{
    LOG_INIT(argc, argv);

    try
    {
        Infrastructure::Log::GetInstance()->Init();
        EventEngine::Context::GetInstance()->Init();
        RenderingEngine::Context::GetInstance()->Init();
        SceneGraph::Context::GetInstance()->Init();
    }
    catch (const std::exception& e)
    {
        RenderingEngine::Window::GetInstance()->ShowMessage("Initialization Error", e.what());
        return EXIT_FAILURE;
    }

    try
    {
        EventEngine::Dispatch::GetInstance()->DispatchOnEngineStartCallback();

        for (;;)
        {
            EventEngine::Context::GetInstance()->HandleEvents();
            RenderingEngine::Context::GetInstance()->Render();
            RenderingEngine::Window::GetInstance()->SwapBuffers();
            Infrastructure::Time::GetInstance()->PerformTick();

            if (EventEngine::Context::GetInstance()->IsQuitRequested()) break;
        }

        EventEngine::Dispatch::GetInstance()->DispatchOnEngineStopCallback();
    }
    catch (const std::exception& e)
    {
        RenderingEngine::Window::GetInstance()->ShowMessage("Error", e.what());
        return EXIT_FAILURE;
    }

    SceneGraph::Context::GetInstance()->Quit();
    RenderingEngine::Context::GetInstance()->Quit();
    EventEngine::Context::GetInstance()->Quit();
    return EXIT_SUCCESS;
}
