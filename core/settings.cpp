// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/settings.hpp>

#include <cstdlib>
#include <string>
#include <vector>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/settings_registry.hpp>

namespace core
{
    namespace
    {
        constexpr const char* k_settings_file_name = "settings.json";

        // <pref directory>/settings.json, or empty when the platform has no
        // per-user directory for the application.
        std::string default_settings_path(const std::filesystem::path& pref_directory)
        {
            if (pref_directory.empty())
            {
                LOG_WRN("settings: no per-user preference directory; no settings file will be read");
                return {};
            }
            return os::path_to_utf8(pref_directory / k_settings_file_name);
        }

        // Reads the file at `path` and applies it as a settings.json document.
        // A missing file is the normal first run and is only noted; the
        // warnings for a malformed one come from settings_registry::apply_json,
        // right after the INFO line that names the file.
        void apply_settings_file(const settings_registry& registry, const std::string& path)
        {
            std::string text;
            if (!os::read_text_file(os::utf8_path(path), text))
            {
                LOG_INF("Settings: no settings file at %s; continuing with the defaults", path.c_str());
                return;
            }
            LOG_INF("Settings: reading %s", path.c_str());
            registry.apply_json(text);
        }

        constexpr const char k_help_preamble[] = "Usage: AlphaEngine [options]\n\nOptions:\n";

        constexpr const char k_meta_options_help[] =
            "  --log-level <spec>       log level, e.g. warn or info,gpu=trace\n"
            "  --settings <path>        settings file to read instead of the default\n"
            "  -h, --help               print this text and exit\n";

        constexpr const char k_help_epilogue[] =
            "\nEvery option also accepts the --key=value form. Command-line values override\n"
            "the ALPHAENGINE_* environment variables, which override the settings file.\n";
    } // namespace

    const char* settings_help_preamble() noexcept
    {
        return k_help_preamble;
    }

    const char* settings_meta_options_help() noexcept
    {
        return k_meta_options_help;
    }

    const char* settings_help_epilogue() noexcept
    {
        return k_help_epilogue;
    }

    settings_load_result load_settings(const settings_registry& registry,
                                       int argc,
                                       char* const argv[],
                                       const std::function<std::filesystem::path()>& pref_directory)
    {
        settings_load_result result;

        std::vector<const char*> args;
        if (argv != nullptr)
        {
            for (int i = 1; i < argc; ++i)
            {
                if (argv[i] != nullptr)
                {
                    args.push_back(argv[i]);
                }
            }
        }

        // The command line is parsed first — --settings names the file the
        // next layer reads and --help short-circuits everything — but applied
        // last, so it stays the top layer.
        const settings_command_line_result options = registry.parse_command_line(args);
        if (options.help_requested)
        {
            result.help_requested = true;
            return result;
        }

        const std::string path =
            options.settings_path.has_value()
                ? *options.settings_path
                : default_settings_path(pref_directory ? pref_directory() : std::filesystem::path{});
        if (!path.empty())
        {
            apply_settings_file(registry, path);
        }
        registry.apply_environment([](const char* name) { return std::getenv(name); });
        registry.apply_command_line(options);

        if (options.log_level.has_value())
        {
            if (logging::configure_levels(*options.log_level))
            {
                LOG_INF("Log level: %s (--log-level=%s)",
                        logging::verbosity_name(logging::level()),
                        options.log_level->c_str());
            }
            else
            {
                LOG_WRN("Unrecognised --log-level='%s'; expected <level>[,<category>=<level>...] with level in "
                        "trace|debug|info|warn|error|fatal; keeping %s",
                        options.log_level->c_str(),
                        logging::verbosity_name(logging::level()));
            }
        }

        registry.log_all_resolved();
        return result;
    }
} // namespace core
