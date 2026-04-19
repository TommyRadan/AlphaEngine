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

#include <SDL3/SDL_log.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <thread>

#include <infrastructure/log.hpp>
#include <infrastructure/version.hpp>

namespace
{
    // Per-message file/line carried from the LOG_* macro down into the SDL log
    // output callback. SDL's public logging API does not surface the origin of
    // the call, so we stash it in thread-local storage for the duration of a
    // single SDL_LogMessageV() invocation.
    thread_local const char* tls_file = nullptr;
    thread_local unsigned tls_line = 0;

    const char* priority_label(SDL_LogPriority priority)
    {
        switch (priority)
        {
        case SDL_LOG_PRIORITY_TRACE:
            return "TRC";
        case SDL_LOG_PRIORITY_VERBOSE:
            return "VRB";
        case SDL_LOG_PRIORITY_DEBUG:
            return "DBG";
        case SDL_LOG_PRIORITY_INFO:
            return "INF";
        case SDL_LOG_PRIORITY_WARN:
            return "WRN";
        case SDL_LOG_PRIORITY_ERROR:
            return "ERR";
        case SDL_LOG_PRIORITY_CRITICAL:
            return "FTL";
        default:
            return "???";
        }
    }

    SDL_LogPriority verbosity_to_sdl(infrastructure::logging::verbosity verbosity)
    {
        switch (verbosity)
        {
        case infrastructure::logging::verbosity::info:
            return SDL_LOG_PRIORITY_INFO;
        case infrastructure::logging::verbosity::warn:
            return SDL_LOG_PRIORITY_WARN;
        case infrastructure::logging::verbosity::error:
            return SDL_LOG_PRIORITY_ERROR;
        case infrastructure::logging::verbosity::fatal:
            return SDL_LOG_PRIORITY_CRITICAL;
        default:
            return SDL_LOG_PRIORITY_INFO;
        }
    }

    void SDLCALL log_output_callback(void* /*userdata*/,
                                     int /*category*/,
                                     SDL_LogPriority priority,
                                     const char* message)
    {
        // Timestamp with millisecond precision.
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_buf);
#endif

        char timestamp[32];
        std::snprintf(timestamp,
                      sizeof(timestamp),
                      "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                      tm_buf.tm_year + 1900,
                      tm_buf.tm_mon + 1,
                      tm_buf.tm_mday,
                      tm_buf.tm_hour,
                      tm_buf.tm_min,
                      tm_buf.tm_sec,
                      static_cast<long long>(ms));

        std::ostringstream tid_stream;
        tid_stream << std::this_thread::get_id();

        const char* file = tls_file != nullptr ? tls_file : "?";
        const unsigned line = tls_line;

        std::fprintf(stderr,
                     "%s [%s] [tid=%s] %s:%u | %s\n",
                     timestamp,
                     priority_label(priority),
                     tid_stream.str().c_str(),
                     file,
                     line,
                     message);
        std::fflush(stderr);
    }
} // namespace

void infrastructure::logging::init(int /*argc*/, char** /*argv*/)
{
    // Install our custom output callback and make sure startup messages are not
    // filtered out by SDL's default priority table.
    SDL_SetLogOutputFunction(&log_output_callback, nullptr);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    LOG_INF("AlphaEngine v%s starting ...", infrastructure::version::get_version().c_str());
}

void infrastructure::logging::message(
    enum verbosity verbosity, const char* file, unsigned line, const char* format, ...)
{
    tls_file = file;
    tls_line = line;

    va_list args;
    va_start(args, format);
    // Forward the va_list unchanged — SDL consumes it internally. Do not call
    // va_arg here and do not copy/advance it; that regresses issue #30.
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, verbosity_to_sdl(verbosity), format, args);
    va_end(args);

    tls_file = nullptr;
    tls_line = 0;

    if (verbosity == verbosity::fatal)
    {
        std::abort();
    }
}
