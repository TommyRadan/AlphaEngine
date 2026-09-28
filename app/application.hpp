// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file application.hpp
 * @brief The engine as a process: the project it runs, the engine that runs
 *        it, the frame loop, and the start-up and shutdown around them.
 *
 * An executable's @c main is an application:
 * @code
 * int main(int argc, char* argv[])
 * {
 *     app::application application{argc, argv};
 *     return application.run();
 * }
 * @endcode
 * The stock @c AlphaEngine executable is exactly that, and a game that
 * wants an executable of its own writes the same few lines and links the
 * engine libraries and its game modules.
 */

#pragma once

#include <optional>

#include <app/project.hpp>

namespace app
{
    /**
     * @brief Runs a project from start-up to shutdown.
     *
     * @ref run picks the project, the first of:
     * 1. the @c --project command-line option — a project file, or a
     *    directory holding @c project.json;
     * 2. the @c ALPHAENGINE_PROJECT environment variable, read the same way;
     * 3. @c project.json beside the executable;
     * 4. the project the executable set with @ref set_default_project.
     *
     * It then resolves the settings — the compiled defaults, the project's
     * content root and default settings, the user's @c settings.json, the
     * environment and the command line, each overriding the one before (see
     * @c core::load_settings) — and answers @c --help. It constructs the
     * engine, with the debug editor drawn over it in Debug builds, brings it
     * up, loads the project's startup scene and installs its game modules in
     * the project's order into the persistent scene, then runs the frame loop
     * until a quit is requested and takes everything down again. A failure at
     * any point is logged, shown in a message box once the window exists, and
     * turned into a failing exit code after an orderly shutdown.
     *
     * One application runs at a time, on the main thread.
     */
    class application
    {
    public:
        /**
         * @brief Keeps @p argc / @p argv for @ref run and prepares the process
         *        for logging: the platform's log sinks (the @c engine.log
         *        mirror beside the executable and SDL's own diagnostics), the
         *        log itself and the crash hook.
         */
        application(int argc, char* argv[]);

        application(const application&) = delete;
        application& operator=(const application&) = delete;
        application(application&&) = delete;
        application& operator=(application&&) = delete;
        ~application() = default;

        /** @brief The project @ref run falls back to when nothing names one (see the class notes). */
        void set_default_project(project value);

        /**
         * @brief Runs the project from start-up to shutdown (see the class
         *        notes).
         * @return The process's exit code: success after a clean run or
         *         @c --help, failure when the project cannot be read, start-up
         *         or the loop fails, or a fatal message — or, with
         *         @c --fail-on-error, any error message — was logged.
         */
        int run();

    private:
        int m_argc;
        char** m_argv;
        std::optional<project> m_default_project;
    };
} // namespace app
