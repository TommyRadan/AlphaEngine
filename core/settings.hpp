// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings.hpp
 * @brief Top-level settings resolution: compiled defaults (each module's own struct), then the caller's base
 *        layers (a project's defaults), then `<pref path>/settings.json`, then the `ALPHAENGINE_*` environment
 *        variables, then the command line, each layer overriding the one before it.
 *
 * Every domain struct (window, graphics, post-processing, input, ...) is owned and defined by the module it
 * configures; this file only drives the resolution and owns the three command-line options that are not
 * backed by any module's struct — `--help`, `--settings` and `--log-level` — since they control the
 * resolution process itself rather than a setting. See @ref core::settings_registry for the generic
 * machinery every module's fields resolve through.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace core
{
    class settings_registry;

    /** @brief Outcome of @ref load_settings. */
    struct settings_load_result
    {
        /**
         * @brief @c --help (or @c -h) was on the command line: the caller prints the assembled `--help` text
         *        (see @ref settings_meta_options_help and @ref settings_registry::help_lines_for) and exits
         *        instead of starting the engine. Every registered field is left at its compiled default.
         */
        bool help_requested{false};
    };

    /**
     * @brief Resolves every field @p registry holds: compiled defaults, then @p base_layers, then `settings.json`
     *        in the per-user preference directory (or the file named by @c --settings), then the `ALPHAENGINE_*`
     *        environment variables, then the command line. A missing or malformed file or layer and any
     *        unrecognised value are logged and skipped, never fatal. Applies @c --log-level to @ref core::logging
     *        on the way and logs every section's resolved values once at INFO
     *        (@ref settings_registry::log_all_resolved). Requires @c LOG_INIT to have run and @p registry to
     *        already hold every module's registered section (see @ref settings_registry::add_section).
     * @param argc           Argument count as given to @c main.
     * @param argv           Arguments as given to @c main; @c argv[0] (the program name) is skipped.
     * @param pref_directory Returns the per-user preference directory, or an empty path when there is none (the
     *                       application passes @c platform::pref_path("AlphaEngine", "AlphaEngine")). Called only when
     *                       the settings file is read from there: not for @c --help, nor with @c --settings.
     * @param base_layers    Documents in the shape of `settings.json`, applied in order on top of the compiled
     *                       defaults and below the settings file: the defaults the application's project declares.
     */
    settings_load_result load_settings(const settings_registry& registry,
                                       int argc,
                                       char* const argv[],
                                       const std::function<std::filesystem::path()>& pref_directory,
                                       std::span<const std::string> base_layers = {});

    /** @brief The literal opening of `--help`'s text: the usage line and the "Options:" header. */
    const char* settings_help_preamble() noexcept;

    /**
     * @brief The literal `--help` text for the three command-line options this file owns directly
     *        (`--log-level`, `--settings`, `-h`/`--help`) — see @ref settings.hpp for why they are not
     *        registered fields. The caller slots this in among the registered sections' own
     *        @c settings_registry::help_lines_for text to assemble the full `--help` output.
     */
    const char* settings_meta_options_help() noexcept;

    /** @brief The literal closing of `--help`'s text: the note on `--key=value` and layer precedence. */
    const char* settings_help_epilogue() noexcept;
} // namespace core
