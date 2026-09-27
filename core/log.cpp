// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/version.hpp>

namespace
{
    using core::logging::record;
    using core::logging::verbosity;

    constexpr const char* k_default_category = "engine";
    constexpr const char* k_level_environment_variable = "ALPHAENGINE_LOG_LEVEL";

#if _DEBUG
    constexpr verbosity k_default_level = verbosity::debug;
#else
    constexpr verbosity k_default_level = verbosity::info;
#endif

    // The output core provides itself: every line to stderr, flushed as it is written.
    struct console_sink : core::logging::sink
    {
        void write(std::string_view line) override
        {
            std::fwrite(line.data(), 1, line.size(), stderr);
            std::fflush(stderr);
        }

        void flush() override
        {
            std::fflush(stderr);
        }
    };

    struct logging_state
    {
        // Sinks: the console, always, then whatever add_sink added. All are written under sink_mutex so concurrent
        // callers (worker threads log too) never interleave a line, and so shutdown() can drop the added sinks
        // while others may still log.
        std::mutex sink_mutex;
        console_sink console;
        std::vector<std::unique_ptr<core::logging::sink>> sinks;

        // Told about every level change (see set_level_observer).
        std::atomic<core::logging::level_observer> level_observer{nullptr};

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

        // Counts of error / fatal messages that actually reached the sinks, for a headless run's exit code.
        std::atomic<std::size_t> error_count{0};
        std::atomic<std::size_t> fatal_count{0};
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

    void notify_level_observer()
    {
        if (const core::logging::level_observer observer = state().level_observer.load(std::memory_order_acquire);
            observer != nullptr)
        {
            observer();
        }
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

    // Delivers one formatted message to every sink and to the in-memory ring.
    void emit(verbosity level, const char* category, const char* file, unsigned line, const char* message)
    {
        // Timestamp with millisecond precision.
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        std::tm tm_buf{};
        core::os::local_time(time_t_now, tm_buf);

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

        std::string text;
        text.append(timestamp)
            .append(" [")
            .append(level_label(level))
            .append("] [")
            .append(category)
            .append("] [tid=")
            .append(tid)
            .append("] ")
            .append(file)
            .append(":")
            .append(std::to_string(line))
            .append(" | ")
            .append(message)
            .append("\n");

        auto& s = state();
        {
            std::lock_guard<std::mutex> lock{s.sink_mutex};
            s.console.write(text);
            for (const std::unique_ptr<core::logging::sink>& destination : s.sinks)
            {
                destination->write(text);
            }
        }

        push_recent(record{now, level, category, file, line, message});
    }

    // Formats @p format with @p args the way vsnprintf does, growing the buffer for a message longer than the
    // stack-sized one. A formatting error yields an empty string rather than a crash.
    std::string format_message(const char* format, va_list args)
    {
        char stack_buffer[1024];
        va_list copy;
        va_copy(copy, args);
        const int needed = std::vsnprintf(stack_buffer, sizeof(stack_buffer), format, copy);
        va_end(copy);
        if (needed < 0)
        {
            return {};
        }
        if (static_cast<std::size_t>(needed) < sizeof(stack_buffer))
        {
            return std::string{stack_buffer, static_cast<std::size_t>(needed)};
        }
        std::vector<char> heap_buffer(static_cast<std::size_t>(needed) + 1);
        va_copy(copy, args);
        std::vsnprintf(heap_buffer.data(), heap_buffer.size(), format, copy);
        va_end(copy);
        return std::string{heap_buffer.data(), static_cast<std::size_t>(needed)};
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

    // Resolve the level: the build-type default first so a re-init is deterministic, then the environment override
    // on top of it.
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
    for (const std::unique_ptr<sink>& destination : s.sinks)
    {
        destination->flush();
    }
    s.sinks.clear();
}

void core::logging::add_sink(std::unique_ptr<sink> destination)
{
    if (destination == nullptr)
    {
        return;
    }
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.sink_mutex};
    s.sinks.push_back(std::move(destination));
}

void core::logging::forward(verbosity level, const char* category, const char* text)
{
    if (category == nullptr)
    {
        category = k_default_category;
    }
    if (!is_enabled(level, category))
    {
        return;
    }
    emit(level, category, "?", 0, text != nullptr ? text : "");
}

void core::logging::set_level_observer(level_observer observer)
{
    state().level_observer.store(observer, std::memory_order_release);
}

void core::logging::flush()
{
    auto& s = state();
    std::lock_guard<std::mutex> lock{s.sink_mutex};
    s.console.flush();
    for (const std::unique_ptr<sink>& destination : s.sinks)
    {
        destination->flush();
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

    va_list args;
    va_start(args, format);
    // Formatted through a va_copy inside format_message; the original list is never advanced here.
    const std::string text = format_message(format != nullptr ? format : "", args);
    va_end(args);

    emit(level, category, file != nullptr ? file : "?", line, text.c_str());

    if (level == verbosity::error)
    {
        state().error_count.fetch_add(1, std::memory_order_relaxed);
    }
    else if (level == verbosity::fatal)
    {
        state().fatal_count.fetch_add(1, std::memory_order_relaxed);
        // The caller throws next; make sure the last line is on disk before the stack unwinds.
        flush();
    }
}

void core::logging::set_level(verbosity level)
{
    state().global_level.store(level, std::memory_order_relaxed);
    notify_level_observer();
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
    notify_level_observer();
}

void core::logging::clear_category_levels()
{
    auto& s = state();
    {
        std::lock_guard<std::mutex> lock{s.level_mutex};
        s.category_levels.clear();
        s.has_category_levels.store(false, std::memory_order_relaxed);
    }
    notify_level_observer();
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

std::size_t core::logging::error_count()
{
    return state().error_count.load(std::memory_order_relaxed);
}

std::size_t core::logging::fatal_count()
{
    return state().fatal_count.load(std::memory_order_relaxed);
}
