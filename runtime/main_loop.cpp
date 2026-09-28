// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/settings.hpp>
#include <core/settings_registry.hpp>
#include <platform/platform.hpp>
#include <platform/window.hpp>
#include <runtime/engine.hpp>
#include <runtime/engine_settings.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI
#include <editor/editor.hpp>
#endif

namespace
{
    // Tears the engine down after a failure. The game lives in the scenes —
    // the game modules' nodes, components and behaviours — so quit() unwinds
    // it while the renderer is still alive: it brings the subsystems down in
    // order, the scenes first, freeing every node. The engine_stop broadcast
    // goes out before that for any listener that holds engine objects of its
    // own. Every subsystem's quit() tolerates an init() that never ran or ran
    // only partway. Errors raised here are logged and swallowed so they
    // cannot mask the failure that brought us here.
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

    // The crash hook runs inside a signal handler, so it stays within what is
    // safe there: one line to stderr. The crash path writes out the log's
    // buffered lines before calling it, so nothing else is lost.
    void on_crash(const char* reason)
    {
        std::fputs("AlphaEngine: fatal ", stderr);
        std::fputs(reason, stderr);
        std::fputs("\n", stderr);
        std::fflush(stderr);
    }
} // namespace

int main(int argc, char* argv[])
{
    // The engine.log mirror and SDL's own diagnostics join the log before
    // its first line.
    platform::install_log_sinks();
    LOG_INIT(argc, argv);
    core::os::install_crash_handler(&on_crash);

    // Every module declares its own settings section against one shared registry (a fixed order that
    // decides --help's layout and the log lines below; see runtime::register_engine_settings), then
    // core::load_settings resolves it: compiled defaults, the settings file, the environment, then the
    // command line. --help is answered here, before the engine and its window are ever constructed.
    runtime::engine_settings engine_settings;
    core::settings_registry registry;
    runtime::register_engine_settings(registry, engine_settings);

    const core::settings_load_result startup =
        core::load_settings(registry, argc, argv, [] { return platform::pref_path("AlphaEngine", "AlphaEngine"); });
    if (startup.help_requested)
    {
        std::fputs(runtime::settings_help_text(registry).c_str(), stdout);
        return EXIT_SUCCESS;
    }

    LOG_INF("Engine starting: initializing subsystems");

    // Construct the owning engine on the stack around the resolved
    // settings. Its constructor installs itself as
    // runtime::current_engine() for the duration of this scope, so every
    // subsystem can resolve its dependencies through the engine.
    runtime::engine engine{std::move(engine_settings)};
#ifdef ALPHAENGINE_HAS_IMGUI
    // Debug builds draw the editor over every frame.
    engine.set_overlay(editor::create_overlay());
#endif

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
        // engine_start went out (at least partly), so its listeners may hold
        // live engine objects: tell them to let go, then take the subsystems
        // down.
        shut_down_after_failure(engine, true);
        return EXIT_FAILURE;
    }

    LOG_INF("Engine shutting down: tearing down subsystems");
    engine.quit();
    LOG_INF("Engine stopped cleanly");

    // A clean shutdown is not a clean run: [FTL] always fails the process, and with fail_on_error so does any
    // [ERR], which is where Vulkan validation messages arrive.
    const std::size_t fatal_count = core::logging::fatal_count();
    const std::size_t error_count = engine.settings->diagnostics.fail_on_error ? core::logging::error_count() : 0;
    if (fatal_count > 0 || error_count > 0)
    {
        LOG_ERR("Exiting with failure: %zu fatal and %zu error message(s) were logged during the run",
                fatal_count,
                error_count);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
