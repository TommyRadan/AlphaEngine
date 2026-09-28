// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file platform.hpp
 * @brief The operating-system services the engine reaches through SDL3:
 *        the executable and per-user preference directories, the process
 *        environment, and the log sinks the platform adds to
 *        @c core::logging.
 *
 * The platform module is where the engine talks to SDL: this file, the
 * window (window.hpp), the SDL input translation (sdl_input.hpp), the
 * audio device (audio_device.hpp) and dynamic libraries
 * (dynamic_library.hpp).
 * The services that need only the C++ standard library — file I/O, UTF-8
 * paths, reading the environment, local time, the crash hook, directory
 * watching — are in @c core::os (core/os/os.hpp), where
 * core itself can use them.
 */

#pragma once

#include <filesystem>

namespace platform
{
    // -- Paths ---------------------------------------------------------------

    /**
     * @brief The directory the running executable lives in, or an empty path
     *        when the platform cannot say. Without a trailing separator.
     */
    std::filesystem::path base_path();

    /**
     * @brief The per-user, writable preference directory for @p application
     *        under @p organization (created on demand), or an empty path when
     *        the platform has none. Settings and caches live here. Without a
     *        trailing separator.
     */
    std::filesystem::path pref_path(const char* organization, const char* application);

    // -- Environment ---------------------------------------------------------

    /**
     * @brief Sets the environment variable @p name to @p value for this
     *        process, replacing any value it had, so a library that reads the
     *        environment later (the Vulkan loader, say) sees it.
     * @return false when the variable could not be set.
     */
    bool set_environment_variable(const char* name, const char* value);

    // -- Logging -------------------------------------------------------------

    /**
     * @brief Adds the platform's sinks to @c core::logging: the @c engine.log
     *        mirror beside the executable (@ref base_path), which keeps a copy
     *        of the log when the console that launched the engine closes with
     *        it, and the capture of SDL's own diagnostics, logged under the
     *        @c "sdl" category at the level @c core::logging sets for it.
     *
     * Call once, before @c LOG_INIT, so the mirror holds the log from its
     * first line.
     */
    void install_log_sinks();
} // namespace platform
