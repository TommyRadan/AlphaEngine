// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <platform/platform.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <core/log.hpp>
#include <core/os/os.hpp>

namespace platform
{
    namespace
    {
        // The category stamped on the messages SDL emits itself; they arrive with no engine call site attached.
        constexpr const char* k_sdl_category = "sdl";

        core::logging::verbosity level_from_sdl(SDL_LogPriority priority)
        {
            switch (priority)
            {
            case SDL_LOG_PRIORITY_TRACE:
            case SDL_LOG_PRIORITY_VERBOSE:
                return core::logging::verbosity::trace;
            case SDL_LOG_PRIORITY_DEBUG:
                return core::logging::verbosity::debug;
            case SDL_LOG_PRIORITY_INFO:
                return core::logging::verbosity::info;
            case SDL_LOG_PRIORITY_WARN:
                return core::logging::verbosity::warn;
            case SDL_LOG_PRIORITY_ERROR:
                return core::logging::verbosity::error;
            case SDL_LOG_PRIORITY_CRITICAL:
                return core::logging::verbosity::fatal;
            default:
                return core::logging::verbosity::info;
            }
        }

        SDL_LogPriority level_to_sdl(core::logging::verbosity level)
        {
            switch (level)
            {
            case core::logging::verbosity::trace:
                return SDL_LOG_PRIORITY_VERBOSE;
            case core::logging::verbosity::debug:
                return SDL_LOG_PRIORITY_DEBUG;
            case core::logging::verbosity::info:
                return SDL_LOG_PRIORITY_INFO;
            case core::logging::verbosity::warn:
                return SDL_LOG_PRIORITY_WARN;
            case core::logging::verbosity::error:
                return SDL_LOG_PRIORITY_ERROR;
            case core::logging::verbosity::fatal:
                return SDL_LOG_PRIORITY_CRITICAL;
            }
            return SDL_LOG_PRIORITY_INFO;
        }

        void SDLCALL sdl_log_output(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message)
        {
            core::logging::forward(level_from_sdl(priority), k_sdl_category, message != nullptr ? message : "");
        }

        // Mirrors the level configured for the "sdl" category (the global level unless overridden) into SDL's own
        // priority filter, so SDL does not format messages the engine would drop anyway. Engine messages never pass
        // through that filter: core::logging applies the precise per-category level itself.
        void apply_sdl_log_priorities()
        {
            SDL_SetLogPriorities(level_to_sdl(core::logging::level_for(k_sdl_category)));
        }

        // The engine.log mirror. The logger flushes it after every batch of lines it hands over, the crash path's
        // batch included, so the file holds everything that reached stderr.
        struct file_sink : core::logging::sink
        {
            explicit file_sink(std::FILE* file) : m_file{file} {}

            ~file_sink() override
            {
                std::fflush(m_file);
                std::fclose(m_file);
            }

            file_sink(const file_sink&) = delete;
            file_sink& operator=(const file_sink&) = delete;
            file_sink(file_sink&&) = delete;
            file_sink& operator=(file_sink&&) = delete;

            void write(std::string_view lines) override
            {
                std::fwrite(lines.data(), 1, lines.size(), m_file);
            }

            void flush() override
            {
                std::fflush(m_file);
            }

        private:
            std::FILE* m_file;
        };

        // A path SDL hands back always carries a trailing separator; strip it
        // so `path / "file"` produces a single one.
        std::filesystem::path path_from_sdl(const char* utf8)
        {
            std::string text{utf8};
            while (text.size() > 1 && (text.back() == '/' || text.back() == '\\'))
            {
                text.pop_back();
            }
            return core::os::utf8_path(text);
        }
    } // namespace

    std::filesystem::path base_path()
    {
        const char* path = SDL_GetBasePath();
        if (path == nullptr)
        {
            return {};
        }
        return path_from_sdl(path);
    }

    std::filesystem::path pref_path(const char* organization, const char* application)
    {
        char* path = SDL_GetPrefPath(organization, application);
        if (path == nullptr)
        {
            return {};
        }
        std::filesystem::path result = path_from_sdl(path);
        SDL_free(path);
        return result;
    }

    std::filesystem::path content_root()
    {
        static const std::filesystem::path root = locate_content_root(base_path());
        return root;
    }

    std::filesystem::path locate_content_root(const std::filesystem::path& base_path)
    {
        std::error_code error;
        for (std::filesystem::path directory = base_path; !directory.empty(); directory = directory.parent_path())
        {
            const std::filesystem::path candidate = directory / "content";
            if (std::filesystem::is_directory(candidate, error))
            {
                return candidate;
            }
            if (directory.parent_path() == directory)
            {
                break;
            }
        }
        return base_path / "content";
    }

    bool set_environment_variable(const char* name, const char* value)
    {
        return SDL_setenv_unsafe(name, value, 1) == 0;
    }

    void install_log_sinks()
    {
        const std::filesystem::path directory = base_path();
        if (!directory.empty())
        {
            const std::string path = core::os::path_to_utf8(directory / "engine.log");
            if (std::FILE* file = std::fopen(path.c_str(), "w"); file != nullptr)
            {
                core::logging::add_sink(std::make_unique<file_sink>(file));
            }
        }

        SDL_SetLogOutputFunction(&sdl_log_output, nullptr);
        core::logging::set_level_observer(&apply_sdl_log_priorities);
        apply_sdl_log_priorities();
    }
} // namespace platform
