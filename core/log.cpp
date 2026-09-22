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

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <core/log.hpp>
#include <core/version.hpp>

namespace
{
    using core::logging::record;
    using core::logging::verbosity;

    // The category stamped on messages SDL emits itself; they arrive through the same output callback with no
    // engine call site attached.
    constexpr const char* k_sdl_category = "sdl";
    constexpr const char* k_default_category = "engine";
    constexpr const char* k_level_environment_variable = "ALPHAENGINE_LOG_LEVEL";

#if _DEBUG
    constexpr verbosity k_default_level = verbosity::debug;
#else
    constexpr verbosity k_default_level = verbosity::info;
#endif

    // Per-message origin carried from the LOG_* macro down into the SDL log output callback. SDL's public logging
    // API does not surface the level enum, the category or the call site of a message, so we stash them in
    // thread-local storage for the duration of a single SDL_LogMessageV() invocation. `active` is false while a
    // message SDL originated itself is being delivered.
    struct origin
    {
        bool active = false;
        verbosity level = verbosity::info;
        const char* category = nullptr;
        const char* file = nullptr;
        unsigned line = 0;
    };
    thread_local origin tls_origin{};

    struct logging_state
    {
        // Sinks. stderr and the optional file are written under sink_mutex so concurrent callers (worker threads
        // log too) never interleave a line, and so shutdown() can close the file while others may still log.
        //
        // The file mirrors stderr: when the process is launched by double-click on Windows the console closes the
        // moment AlphaEngine.exe exits, so any shutdown-time logs flash by unread; a copy beside the executable
        // lets us recover the full trace post-mortem. Opened lazily on the first message so init() ordering does
        // not matter; closed by shutdown() and never reopened (reopening would truncate it).
        std::mutex sink_mutex;
        std::FILE* log_file = nullptr;
        bool log_file_attempted = false;

        // Level filter. The global level is read on every message without a lock; the per-category overrides are
        // consulted (under level_mutex) only while at least one exists.
        std::atomic<verbosity> global_level{k_default_level};
        std::atomic<bool> has_category_levels{false};
        std::mutex level_mutex;
        std::vector<std::pair<std::string, verbosity>> category_levels;

        // Bounded ring of the most recent messages for an in-engine console.
        std::mutex ring_mutex;
        std::vector<record> ring;
        std::size_t ring_next = 0;
        std::size_t ring_count = 0;

        std::vector<std::string> arguments;
        bool shutdown_registered = false;
    };

    logging_state& state()
    {
        // Deliberately never destroyed: a message emitted from a static destructor after main() returns must still
        // find live mutexes and sinks.
        static logging_state* instance = new logging_state{};
        return *instance;
    }

    const char* level_label(verbosity level)
    {
        switch (level)
        {
        case verbosity::trace:
            return "TRC";
        case verbosity::debug:
            return "DBG";
        case verbosity::info:
            return "INF";
        case verbosity::warn:
            return "WRN";
        case verbosity::error:
            return "ERR";
        case verbosity::fatal:
            return "FTL";
        }
        return "???";
    }

    SDL_LogPriority verbosity_to_sdl(verbosity level)
    {
        switch (level)
        {
        case verbosity::trace:
            return SDL_LOG_PRIORITY_VERBOSE;
        case verbosity::debug:
            return SDL_LOG_PRIORITY_DEBUG;
        case verbosity::info:
            return SDL_LOG_PRIORITY_INFO;
        case verbosity::warn:
            return SDL_LOG_PRIORITY_WARN;
        case verbosity::error:
            return SDL_LOG_PRIORITY_ERROR;
        case verbosity::fatal:
            return SDL_LOG_PRIORITY_CRITICAL;
        }
        return SDL_LOG_PRIORITY_INFO;
    }

    verbosity verbosity_from_sdl(SDL_LogPriority priority)
    {
        switch (priority)
        {
        case SDL_LOG_PRIORITY_TRACE:
        case SDL_LOG_PRIORITY_VERBOSE:
            return verbosity::trace;
        case SDL_LOG_PRIORITY_DEBUG:
            return verbosity::debug;
        case SDL_LOG_PRIORITY_INFO:
            return verbosity::info;
        case SDL_LOG_PRIORITY_WARN:
            return verbosity::warn;
        case SDL_LOG_PRIORITY_ERROR:
            return verbosity::error;
        case SDL_LOG_PRIORITY_CRITICAL:
            return verbosity::fatal;
        default:
            return verbosity::info;
        }
    }

    // Mirrors the engine's level table into SDL's own priority filter. Every engine message travels through
    // SDL_LOG_CATEGORY_APPLICATION, so that category is opened up to the most permissive level in effect anywhere
    // (message() applies the precise per-category filter before SDL sees the call); SDL's internal categories
    // follow the level configured for "sdl", i.e. the global level unless overridden.
    void apply_sdl_priorities()
    {
        auto& s = state();
        verbosity most_permissive = s.global_level.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock{s.level_mutex};
            for (const auto& [category, level] : s.category_levels)
            {
                most_permissive = std::min(most_permissive, level);
            }
        }
        SDL_SetLogPriorities(verbosity_to_sdl(core::logging::level_for(k_sdl_category)));
        SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, verbosity_to_sdl(most_permissive));
    }

    void push_recent(record&& entry)
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock{s.ring_mutex};
        if (s.ring.size() < core::logging::k_recent_capacity)
        {
            s.ring.resize(core::logging::k_recent_capacity);
        }
        s.ring[s.ring_next] = std::move(entry);
        s.ring_next = (s.ring_next + 1) % core::logging::k_recent_capacity;
        s.ring_count = std::min(s.ring_count + 1, core::logging::k_recent_capacity);
    }

    void SDLCALL log_output_callback(void* /*userdata*/,
                                     int /*category*/,
                                     SDL_LogPriority priority,
                                     const char* message)
    {
        const origin from = tls_origin;
        const verbosity level = from.active ? from.level : verbosity_from_sdl(priority);
        const char* category = from.active ? from.category : k_sdl_category;
        const char* file = from.active && from.file != nullptr ? from.file : "?";
        const unsigned line = from.active ? from.line : 0;

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

        // Sized for the worst case -Wformat-truncation can construct, not the 23 characters a real date needs.
        char timestamp[64];
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
        const std::string tid = tid_stream.str();

        const auto write_to = [&](std::FILE* dst)
        {
            if (dst == nullptr)
            {
                return;
            }
            std::fprintf(dst,
                         "%s [%s] [%s] [tid=%s] %s:%u | %s\n",
                         timestamp,
                         level_label(level),
                         category,
                         tid.c_str(),
                         file,
                         line,
                         message);
            std::fflush(dst);
        };

        auto& s = state();
        {
            std::lock_guard<std::mutex> lock{s.sink_mutex};
            write_to(stderr);

            if (!s.log_file_attempted)
            {
                s.log_file_attempted = true;
                const char* base_path = SDL_GetBasePath();
                if (base_path != nullptr)
                {
                    const std::string path = std::string{base_path} + "engine.log";
                    s.log_file = std::fopen(path.c_str(), "w");
                }
            }
            write_to(s.log_file);
        }

        push_recent(record{now, level, category, file, line, message});
    }

    std::string_view trim(std::string_view text)
    {
        const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!text.empty() && is_space(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && is_space(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    bool equals_ignoring_case(std::string_view a, std::string_view b)
    {
        return a.size() == b.size() &&
               std::equal(a.begin(),
                          a.end(),
                          b.begin(),
                          [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
    }
} // namespace

void core::logging::init(int argc, char* argv[])
{
    auto& s = state();

    s.arguments.clear();
    if (argv != nullptr)
    {
        for (int i = 0; i < argc; ++i)
        {
            if (argv[i] != nullptr)
            {
                s.arguments.emplace_back(argv[i]);
            }
        }
    }

    // Install our custom output callback, then resolve the level: the build-type default first so a re-init is
    // deterministic, then the environment override on top of it.
    SDL_SetLogOutputFunction(&log_output_callback, nullptr);
    clear_category_levels();
    set_level(k_default_level);

    const char* spec = std::getenv(k_level_environment_variable);
    const bool spec_applied = spec == nullptr || configure_levels(spec);

    if (!s.shutdown_registered)
    {
        s.shutdown_registered = true;
        std::atexit(&core::logging::shutdown);
    }

    LOG_INF("AlphaEngine v%s starting ...", core::version::get_version().c_str());
    if (spec == nullptr)
    {
        LOG_INF("Log level: %s", verbosity_name(level()));
    }
    else if (spec_applied)
    {
        LOG_INF("Log level: %s (%s=%s)", verbosity_name(level()), k_level_environment_variable, spec);
    }
    else
    {
        LOG_WRN("Unrecognised %s='%s'; expected <level>[,<category>=<level>...] with level in "
                "trace|debug|info|warn|error|fatal; keeping %s",
                k_level_environment_variable,
                spec,
                verbosity_name(level()));
    }
}

void core::logging::shutdown()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.sink_mutex};
    if (s.log_file != nullptr)
    {
        std::fflush(s.log_file);
        std::fclose(s.log_file);
        s.log_file = nullptr;
    }
    // Stay closed: a late message must not reopen (and truncate) the file.
    s.log_file_attempted = true;
}

void core::logging::flush()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.sink_mutex};
    std::fflush(stderr);
    if (s.log_file != nullptr)
    {
        std::fflush(s.log_file);
    }
}

void core::logging::message(
    verbosity level, const char* category, const char* file, unsigned line, const char* format, ...)
{
    if (category == nullptr)
    {
        category = k_default_category;
    }
    if (!is_enabled(level, category))
    {
        return;
    }

    tls_origin = origin{true, level, category, file, line};

    va_list args;
    va_start(args, format);
    // Forward the va_list unchanged — SDL consumes it internally. Do not call
    // va_arg here and do not copy/advance it; that regresses issue #30.
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, verbosity_to_sdl(level), format, args);
    va_end(args);

    tls_origin = origin{};

    if (level == verbosity::fatal)
    {
        // The caller throws next; make sure the last line is on disk before the stack unwinds.
        flush();
    }
}

void core::logging::set_level(verbosity level)
{
    state().global_level.store(level, std::memory_order_relaxed);
    apply_sdl_priorities();
}

core::logging::verbosity core::logging::level()
{
    return state().global_level.load(std::memory_order_relaxed);
}

void core::logging::set_category_level(const char* category, verbosity level)
{
    if (category == nullptr)
    {
        return;
    }
    auto& s = state();
    {
        std::lock_guard<std::mutex> lock{s.level_mutex};
        auto it = std::find_if(s.category_levels.begin(),
                               s.category_levels.end(),
                               [category](const auto& entry) { return entry.first == category; });
        if (it != s.category_levels.end())
        {
            it->second = level;
        }
        else
        {
            s.category_levels.emplace_back(category, level);
        }
        s.has_category_levels.store(true, std::memory_order_relaxed);
    }
    apply_sdl_priorities();
}

void core::logging::clear_category_levels()
{
    auto& s = state();
    {
        std::lock_guard<std::mutex> lock{s.level_mutex};
        s.category_levels.clear();
        s.has_category_levels.store(false, std::memory_order_relaxed);
    }
    apply_sdl_priorities();
}

core::logging::verbosity core::logging::level_for(const char* category)
{
    auto& s = state();
    if (category != nullptr && s.has_category_levels.load(std::memory_order_relaxed))
    {
        std::lock_guard<std::mutex> lock{s.level_mutex};
        for (const auto& [name, level] : s.category_levels)
        {
            if (name == category)
            {
                return level;
            }
        }
    }
    return s.global_level.load(std::memory_order_relaxed);
}

bool core::logging::is_enabled(verbosity level, const char* category)
{
    return level >= level_for(category);
}

bool core::logging::configure_levels(std::string_view spec)
{
    // Parse everything first so an invalid entry leaves the current configuration untouched.
    std::optional<verbosity> global;
    std::vector<std::pair<std::string, verbosity>> overrides;

    while (!spec.empty())
    {
        const auto comma = spec.find(',');
        const std::string_view entry = trim(spec.substr(0, comma));
        spec = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
        if (entry.empty())
        {
            continue;
        }

        const auto equals = entry.find('=');
        if (equals == std::string_view::npos)
        {
            const auto parsed = parse_verbosity(entry);
            if (!parsed.has_value())
            {
                return false;
            }
            global = parsed;
            continue;
        }

        const std::string_view category = trim(entry.substr(0, equals));
        const auto parsed = parse_verbosity(entry.substr(equals + 1));
        if (category.empty() || !parsed.has_value())
        {
            return false;
        }
        overrides.emplace_back(std::string{category}, *parsed);
    }

    if (global.has_value())
    {
        set_level(*global);
    }
    for (const auto& [category, level] : overrides)
    {
        set_category_level(category.c_str(), level);
    }
    return true;
}

std::optional<core::logging::verbosity> core::logging::parse_verbosity(std::string_view name)
{
    name = trim(name);
    constexpr verbosity k_all_levels[] = {
        verbosity::trace, verbosity::debug, verbosity::info, verbosity::warn, verbosity::error, verbosity::fatal};
    for (const verbosity level : k_all_levels)
    {
        if (equals_ignoring_case(name, verbosity_name(level)))
        {
            return level;
        }
    }
    if (equals_ignoring_case(name, "warning"))
    {
        return verbosity::warn;
    }
    return std::nullopt;
}

const char* core::logging::verbosity_name(verbosity level)
{
    switch (level)
    {
    case verbosity::trace:
        return "trace";
    case verbosity::debug:
        return "debug";
    case verbosity::info:
        return "info";
    case verbosity::warn:
        return "warn";
    case verbosity::error:
        return "error";
    case verbosity::fatal:
        return "fatal";
    }
    return "unknown";
}

std::vector<core::logging::record> core::logging::recent_messages()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.ring_mutex};
    std::vector<record> out;
    out.reserve(s.ring_count);
    const std::size_t oldest = (s.ring_next + k_recent_capacity - s.ring_count) % k_recent_capacity;
    for (std::size_t i = 0; i < s.ring_count; ++i)
    {
        out.push_back(s.ring[(oldest + i) % k_recent_capacity]);
    }
    return out;
}

void core::logging::clear_recent_messages()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.ring_mutex};
    s.ring_next = 0;
    s.ring_count = 0;
}

const std::vector<std::string>& core::logging::arguments()
{
    return state().arguments;
}
