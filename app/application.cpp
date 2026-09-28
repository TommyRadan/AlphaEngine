// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <app/application.hpp>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/settings.hpp>
#include <core/settings_registry.hpp>
#include <platform/platform.hpp>
#include <platform/window.hpp>
#include <runtime/engine.hpp>
#include <runtime/engine_settings.hpp>
#include <runtime/game_module.hpp>
#include <runtime/scene_manager.hpp>
#include <runtime/scene_serializer.hpp>

#ifdef ALPHAENGINE_HAS_IMGUI
#include <editor/editor.hpp>
#endif

namespace app
{
    namespace
    {
        constexpr std::string_view k_project_option = "--project";
        constexpr const char* k_project_variable = "ALPHAENGINE_PROJECT";
        constexpr const char k_project_help[] =
            "  --project <path>         project to run: a project file, or a directory holding project.json\n";

        // The command line with --project taken out. The option picks the
        // project, and the project provides one of the layers the settings
        // resolve through, so it is read before them and the settings
        // registry never sees it.
        struct command_line
        {
            // argv[0] and every other argument, in their order.
            std::vector<char*> arguments;
            // The last --project value, if any.
            std::optional<std::string> project;
        };

        command_line split_project_option(int argc, char* argv[])
        {
            command_line result;
            for (int index = 0; index < argc; ++index)
            {
                char* argument = argv[index];
                if (argument == nullptr)
                {
                    continue;
                }
                const std::string_view text{argument};
                if (index == 0 || !text.starts_with(k_project_option))
                {
                    result.arguments.push_back(argument);
                    continue;
                }
                std::string_view value;
                if (text.size() == k_project_option.size())
                {
                    if (index + 1 < argc && argv[index + 1] != nullptr)
                    {
                        value = argv[++index];
                    }
                }
                else if (text[k_project_option.size()] == '=')
                {
                    value = text.substr(k_project_option.size() + 1);
                }
                else
                {
                    // Some other option that merely starts with "--project".
                    result.arguments.push_back(argument);
                    continue;
                }
                if (value.empty())
                {
                    LOG_WRN("--project needs a path; ignoring it");
                    continue;
                }
                result.project = std::string{value};
            }
            return result;
        }

        // The project to run, in the order application documents; std::nullopt
        // with the reason in @p error when there is none or it cannot be read.
        std::optional<project> choose_project(const std::optional<std::string>& option,
                                              const std::optional<project>& fallback,
                                              std::string& error)
        {
            if (option.has_value())
            {
                return read_project(core::os::utf8_path(*option), error);
            }
            if (const std::optional<std::string> variable = core::os::environment_variable(k_project_variable);
                variable.has_value() && !variable->empty())
            {
                return read_project(core::os::utf8_path(*variable), error);
            }
            if (const std::filesystem::path base_path = platform::base_path();
                !base_path.empty() && core::os::file_exists(base_path / k_project_file_name))
            {
                return read_project(base_path / k_project_file_name, error);
            }
            if (fallback.has_value())
            {
                return fallback;
            }
            error = "there is no project to run: name one with --project <path> or ALPHAENGINE_PROJECT, or place " +
                    std::string{k_project_file_name} + " beside the executable";
            return std::nullopt;
        }

        // The settings layers the project provides, in the order they apply:
        // its content root as content.root, then its own default settings.
        std::vector<std::string> settings_layers(const project& value)
        {
            std::vector<std::string> layers;
            if (!value.content_root.empty())
            {
                nlohmann::json content;
                content["content"]["root"] = core::os::path_to_utf8(value.content_root);
                layers.push_back(content.dump());
            }
            if (!value.settings.empty())
            {
                layers.push_back(value.settings);
            }
            return layers;
        }

        // True when this executable has every game module @p value names;
        // otherwise logs the ones it lacks and the ones it has.
        bool has_every_module(const project& value)
        {
            std::string missing;
            for (const std::string& name : value.modules)
            {
                if (!runtime::has_game_module(name))
                {
                    missing += missing.empty() ? name : ", " + name;
                }
            }
            if (missing.empty())
            {
                return true;
            }
            std::string available;
            for (const std::string& name : runtime::game_module_names())
            {
                available += available.empty() ? name : ", " + name;
            }
            LOG_ERR("Project '%s' names game modules this executable does not have: %s (it has: %s)",
                    value.name.c_str(),
                    missing.c_str(),
                    available.empty() ? "none" : available.c_str());
            return false;
        }

        // Puts the project into the engine, which is up: its startup scene,
        // then its game modules' bootstraps, in the project's order, into the
        // persistent scene, which outlives any scene loaded later.
        void install_project(runtime::engine& engine, const project& value)
        {
            if (!value.startup_scene.empty() &&
                runtime::load_scene(*engine.scenes, core::os::utf8_path(value.startup_scene)) == nullptr)
            {
                throw std::runtime_error{"the startup scene " + value.startup_scene + " could not be loaded"};
            }
            runtime::scene& persistent = engine.scenes->persistent_scene();
            for (const std::string& name : value.modules)
            {
                runtime::install_game_module(name, persistent);
            }
        }

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
        // safe there: one line to stderr. Every logged line is already flushed to
        // engine.log as it is written, so nothing else is lost.
        void on_crash(const char* reason)
        {
            std::fputs("AlphaEngine: fatal ", stderr);
            std::fputs(reason, stderr);
            std::fputs("\n", stderr);
            std::fflush(stderr);
        }
    } // namespace

    application::application(int argc, char* argv[]) : m_argc{argc}, m_argv{argv}
    {
        // The engine.log mirror and SDL's own diagnostics join the log before
        // its first line.
        platform::install_log_sinks();
        LOG_INIT(argc, argv);
        core::os::install_crash_handler(&on_crash);
    }

    void application::set_default_project(project value)
    {
        m_default_project = std::move(value);
    }

    int application::run()
    {
        const command_line options = split_project_option(m_argc, m_argv);
        std::string project_error;
        const std::optional<project> selected = choose_project(options.project, m_default_project, project_error);
        if (selected.has_value())
        {
            LOG_INF("Project: '%s' from %s",
                    selected->name.c_str(),
                    selected->file.empty() ? "the executable" : core::os::path_to_utf8(selected->file).c_str());
        }

        // Every module declares its own settings section against one shared registry (a fixed order that
        // decides --help's layout and the log lines below; see runtime::register_engine_settings), then
        // core::load_settings resolves it: compiled defaults, the project's, the settings file, the environment,
        // then the command line. --help is answered here, before the engine and its window are ever constructed.
        runtime::engine_settings engine_settings;
        core::settings_registry registry;
        runtime::register_engine_settings(registry, engine_settings);

        const std::vector<std::string> project_layers =
            selected.has_value() ? settings_layers(*selected) : std::vector<std::string>{};
        const core::settings_load_result startup = core::load_settings(
            registry,
            static_cast<int>(options.arguments.size()),
            options.arguments.data(),
            [] { return platform::pref_path("AlphaEngine", "AlphaEngine"); },
            project_layers);
        if (startup.help_requested)
        {
            std::fputs(runtime::settings_help_text(registry, k_project_help).c_str(), stdout);
            return EXIT_SUCCESS;
        }

        if (!selected.has_value())
        {
            LOG_ERR("Cannot start: %s", project_error.c_str());
            return EXIT_FAILURE;
        }
        if (!has_every_module(*selected))
        {
            return EXIT_FAILURE;
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
            install_project(engine, *selected);
        }
        catch (const std::exception& e)
        {
            LOG_ERR("Start-up failed: %s", e.what());
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
} // namespace app
