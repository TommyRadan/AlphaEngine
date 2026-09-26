/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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

#include <core/platform/platform.hpp>

#include <atomic>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

// The SDL-backed half of the platform layer: the clock, the well-known
// directories and the capture of SDL's own log output. This is the one
// translation unit in core (besides dynamic_library.cpp) that includes SDL.

namespace core::platform
{
    namespace
    {
        std::atomic<native_log_sink> g_native_log_sink{nullptr};

        native_log_level level_from_sdl(SDL_LogPriority priority)
        {
            switch (priority)
            {
            case SDL_LOG_PRIORITY_TRACE:
            case SDL_LOG_PRIORITY_VERBOSE:
                return native_log_level::trace;
            case SDL_LOG_PRIORITY_DEBUG:
                return native_log_level::debug;
            case SDL_LOG_PRIORITY_INFO:
                return native_log_level::info;
            case SDL_LOG_PRIORITY_WARN:
                return native_log_level::warn;
            case SDL_LOG_PRIORITY_ERROR:
                return native_log_level::error;
            case SDL_LOG_PRIORITY_CRITICAL:
                return native_log_level::fatal;
            default:
                return native_log_level::info;
            }
        }

        SDL_LogPriority level_to_sdl(native_log_level level)
        {
            switch (level)
            {
            case native_log_level::trace:
                return SDL_LOG_PRIORITY_VERBOSE;
            case native_log_level::debug:
                return SDL_LOG_PRIORITY_DEBUG;
            case native_log_level::info:
                return SDL_LOG_PRIORITY_INFO;
            case native_log_level::warn:
                return SDL_LOG_PRIORITY_WARN;
            case native_log_level::error:
                return SDL_LOG_PRIORITY_ERROR;
            case native_log_level::fatal:
                return SDL_LOG_PRIORITY_CRITICAL;
            }
            return SDL_LOG_PRIORITY_INFO;
        }

        void SDLCALL sdl_log_output(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message)
        {
            if (const native_log_sink sink = g_native_log_sink.load(std::memory_order_acquire); sink != nullptr)
            {
                sink(level_from_sdl(priority), message != nullptr ? message : "");
            }
        }

        // A path SDL hands back always carries a trailing separator; strip it
        // so `path / "file"` produces a single one.
        std::filesystem::path path_from_sdl(const char* utf8)
        {
            std::string text{utf8};
            while (text.size() > 1 && (text.back() == '/' || text.back() == '\\'))
            {
                text.pop_back();
            }
            return utf8_path(text);
        }
    } // namespace

    uint64_t performance_counter()
    {
        return SDL_GetPerformanceCounter();
    }

    uint64_t performance_frequency()
    {
        const uint64_t frequency = SDL_GetPerformanceFrequency();
        return frequency != 0 ? frequency : 1;
    }

    uint64_t ticks_ms()
    {
        return SDL_GetTicks();
    }

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

    void set_native_log_sink(native_log_sink sink)
    {
        g_native_log_sink.store(sink, std::memory_order_release);
        if (sink != nullptr)
        {
            SDL_SetLogOutputFunction(&sdl_log_output, nullptr);
        }
        else
        {
            SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
        }
    }

    void set_native_log_level(native_log_level minimum)
    {
        SDL_SetLogPriorities(level_to_sdl(minimum));
    }
} // namespace core::platform
