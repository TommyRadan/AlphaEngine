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

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <rendering_engine/window.hpp>

int main(int argc, char* argv[])
{
    LOG_INIT(argc, argv);

    LOG_INF("Engine starting: initializing subsystems");

    // Construct the owning engine on the stack. Its constructor
    // installs itself as control::current_engine() for the duration
    // of this scope, so every subsystem that used to pull its
    // dependency out of a singleton can resolve it from the engine.
    control::engine engine;

    try
    {
        engine.init();
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Subsystem initialization failed: %s", e.what());
        if (engine.window != nullptr)
        {
            engine.window->show_message("Initialization Error", e.what());
        }
        return EXIT_FAILURE;
    }

    LOG_INF("Engine initialized: entering main loop");

    try
    {
        engine.broadcast_engine_start();

        while (!engine.is_quit_requested())
        {
            engine.tick();
        }

        LOG_INF("Quit requested: broadcasting engine_stop");
        engine.broadcast_engine_stop();
    }
    catch (const std::exception& e)
    {
        LOG_ERR("Unrecoverable error in main loop: %s", e.what());
        if (engine.window != nullptr)
        {
            engine.window->show_message("Error", e.what());
        }
        return EXIT_FAILURE;
    }

    LOG_INF("Engine shutting down: tearing down subsystems");
    engine.quit();
    LOG_INF("Engine stopped cleanly");
    return EXIT_SUCCESS;
}
