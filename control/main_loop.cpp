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
#include <infrastructure/log.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/window.hpp>
#include <scene_graph/scene_graph.hpp>

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
        rendering_engine::context::get_instance().init();
        scene_graph::context::get_instance().init();
    }
    catch (const std::exception& e)
    {
        rendering_engine::window::get_instance().show_message("Initialization Error", e.what());
        return EXIT_FAILURE;
    }

    try
    {
        event_engine::context::get_instance().register_listener(event_engine::event_type::quit_requested,
                                                                quit_request_listener);
        event_engine::context::get_instance().broadcast(event_engine::engine_start());

        for (;;)
        {
            rendering_engine::window::get_instance().tick();
            rendering_engine::context::get_instance().render();
            rendering_engine::window::get_instance().swap_buffers();
            infrastructure::time::get_instance().perform_tick();

            if (is_quit_requested)
                break;
        }

        event_engine::context::get_instance().broadcast(event_engine::engine_stop());
    }
    catch (const std::exception& e)
    {
        rendering_engine::window::get_instance().show_message("Error", e.what());
        return EXIT_FAILURE;
    }

    scene_graph::context::get_instance().quit();
    rendering_engine::context::get_instance().quit();
    event_engine::context::get_instance().quit();
    return EXIT_SUCCESS;
}
