/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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

#include <stdexcept>

#include <event_engine/event_engine.hpp>
#include <RenderingEngine/RenderingEngine.hpp>
#include <RenderingEngine/Window.hpp>
#include <Infrastructure/log.hpp>
#include <Infrastructure/Time.hpp>
#include <SceneGraph/SceneGraph.hpp>

bool is_quit_requested = false;

void quit_request_listener(const event_engine::event& event)
{
    is_quit_requested = true;
}

int main(int argc, char* argv[])
{
    LOG_INIT(argc, argv);

    try
    {
        event_engine::context::get_instance().init();
        RenderingEngine::Context::get_instance().Init();
        SceneGraph::Context::get_instance().Init();
    }
    catch (const std::exception& e)
    {
        RenderingEngine::Window::get_instance().ShowMessage("Initialization Error", e.what());
        return EXIT_FAILURE;
    }

    try
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::quit_requested, quit_request_listener);
        event_engine::context::get_instance().broadcast(event_engine::engine_start());

        for (;;)
        {
            RenderingEngine::Window::get_instance().Tick();
            RenderingEngine::Context::get_instance().Render();
            RenderingEngine::Window::get_instance().SwapBuffers();
            Infrastructure::Time::get_instance().PerformTick();

            if (is_quit_requested) break;
        }

        event_engine::context::get_instance().broadcast(event_engine::engine_stop());
    }
    catch (const std::exception& e)
    {
        RenderingEngine::Window::get_instance().ShowMessage("Error", e.what());
        return EXIT_FAILURE;
    }

    SceneGraph::Context::get_instance().Quit();
    RenderingEngine::Context::get_instance().Quit();
    event_engine::context::get_instance().quit();
    return EXIT_SUCCESS;
}
