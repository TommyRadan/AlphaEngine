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

#include <core/log.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Tears the engine down after a failure. The engine on main's stack dies
    // before the game modules' file-scope statics, so those statics — nodes,
    // lights, models — would otherwise unwind against freed subsystems: the
    // engine_stop broadcast is what makes the modules release them while the
    // renderer and scene are still alive, and quit() then brings the
    // subsystems down in order. Every subsystem's quit() tolerates an init()
    // that never ran or ran only partway. Errors raised here are logged and
    // swallowed so they cannot mask the failure that brought us here.
    void shut_down_after_failure(runtime::engine& engine, bool started)
    {
        if (started)
        {
            try
            {
                engine.broadcast_engine_stop();
            }
            catch (const std::exception& e)
            {
                LOG_ERR("engine_stop listener failed during error shutdown: %s", e.what());
            }
            catch (...)
            {
                LOG_ERR("engine_stop listener failed during error shutdown");
            }
        }

        try
        {
            engine.quit();
        }
        catch (const std::exception& e)
        {
            LOG_ERR("Subsystem teardown failed during error shutdown: %s", e.what());
        }
        catch (...)
        {
            LOG_ERR("Subsystem teardown failed during error shutdown");
        }
    }
} // namespace

int main(int argc, char* argv[])
{
    LOG_INIT(argc, argv);

    LOG_INF("Engine starting: initializing subsystems");

    // Construct the owning engine on the stack. Its constructor
    // installs itself as runtime::current_engine() for the duration
    // of this scope, so every subsystem that used to pull its
    // dependency out of a singleton can resolve it from the engine.
    runtime::engine engine;

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
        // Nothing was started, so there is no engine_stop to deliver; the
        // subsystems that did come up still need taking down in order.
        shut_down_after_failure(engine, false);
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
        // engine_start went out (at least partly), so the modules hold live
        // engine objects: tell them to let go, then take the subsystems down.
        shut_down_after_failure(engine, true);
        return EXIT_FAILURE;
    }

    LOG_INF("Engine shutting down: tearing down subsystems");
    engine.quit();
    LOG_INF("Engine stopped cleanly");
    return EXIT_SUCCESS;
}
