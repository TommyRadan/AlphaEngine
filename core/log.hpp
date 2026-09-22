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

/**
 * @file log.hpp
 * @brief Printf-style logging facade backed by SDL's logging API.
 *
 * Prefer the @c LOG_TRC / @c LOG_DBG / @c LOG_INF / @c LOG_WRN / @c LOG_ERR / @c LOG_FTL macros over calling
 * @ref core::logging::message directly — they capture the originating @c __FILE__ / @c __LINE__ and the
 * translation unit's @c LOG_CATEGORY automatically.
 *
 * Categories: every message carries a category name, @c "engine" unless the translation unit says otherwise.
 * To tag a file's messages, define @c LOG_CATEGORY before its first include (headers may pull this file in
 * transitively, so it has to come first):
 * @code
 * #define LOG_CATEGORY "gpu"
 * #include <core/log.hpp>
 * @endcode
 *
 * Levels: messages below the effective level are dropped before formatting. The level is resolved by
 * @ref core::logging::init from the build type (@c debug in Debug builds, @c info otherwise) and the
 * @c ALPHAENGINE_LOG_LEVEL environment variable — a level name, optionally followed by per-category overrides,
 * e.g. @c "info,gpu=trace". Level names are case-insensitive.
 *
 * Fatal: @c LOG_FTL logs and flushes the sinks but does @b not terminate the process. The call site throws (or
 * exits) right after it; that is what lets @c main unwind, show the error dialog and tear the engine down.
 *
 * Every message that reaches a sink is also kept in a bounded in-memory ring (@ref core::logging::recent_messages)
 * so a debug console can display the recent log without re-parsing the file.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Marks a variadic function as taking a printf-style format string so GCC / Clang validate the conversions
 * at every call site (-Wformat). @p format_index is the 1-based position of the format parameter and
 * @p first_argument_index that of the first variadic argument. MSVC has no equivalent outside /analyze, so the
 * macro expands to nothing there.
 */
#if defined(__MINGW32__) && defined(__MINGW_PRINTF_FORMAT)
#define ALPHAENGINE_PRINTF_FORMAT(format_index, first_argument_index)                                                  \
    __attribute__((format(__MINGW_PRINTF_FORMAT, format_index, first_argument_index)))
#elif defined(__GNUC__) || defined(__clang__)
#define ALPHAENGINE_PRINTF_FORMAT(format_index, first_argument_index)                                                  \
    __attribute__((format(printf, format_index, first_argument_index)))
#else
#define ALPHAENGINE_PRINTF_FORMAT(format_index, first_argument_index)
#endif

/**
 * @brief The category the LOG_* macros stamp on every message from this translation unit. Defaults to
 * @c "engine"; a file overrides it by defining the macro before its first include.
 */
#ifndef LOG_CATEGORY
#define LOG_CATEGORY "engine"
#endif

/** @brief Initializes the logging system. Wraps @ref core::logging::init. */
#define LOG_INIT(argc, argv) core::logging::init(argc, argv)

/** @brief Logs a trace message (printf-style): per-frame / per-draw detail, off unless explicitly enabled. */
#define LOG_TRC(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::trace, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs a debug message (printf-style): resource loads, state changes; on by default in Debug builds. */
#define LOG_DBG(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::debug, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs an informational message (printf-style). */
#define LOG_INF(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::info, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs a warning message (printf-style). */
#define LOG_WRN(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::warn, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs an error message (printf-style). */
#define LOG_ERR(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::error, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)
/**
 * @brief Logs a fatal message (printf-style) and flushes the sinks. It does not terminate the process: the call
 * site must throw (or exit) immediately afterwards.
 */
#define LOG_FTL(...)                                                                                                   \
    core::logging::message(core::logging::verbosity::fatal, LOG_CATEGORY, __FILE__, __LINE__, __VA_ARGS__)

namespace core
{
    /**
     * @brief The logging system
     */
    namespace logging
    {
        /** @brief Severity levels accepted by @ref message, least to most severe. */
        enum class verbosity
        {
            trace,
            debug,
            info,
            warn,
            error,
            fatal
        };

        /** @brief One message as retained by the in-memory ring (see @ref recent_messages). */
        struct record
        {
            std::chrono::system_clock::time_point timestamp;
            verbosity level = verbosity::info;
            std::string category;
            std::string file;
            unsigned line = 0;
            std::string text;
        };

        /** @brief How many messages @ref recent_messages retains; older ones are dropped. */
        inline constexpr std::size_t k_recent_capacity = 512;

        /**
         * @brief Initializes the logging system: installs the SDL output callback, stashes the command line
         * (see @ref arguments), resolves the level from the build type and @c ALPHAENGINE_LOG_LEVEL, and
         * registers @ref shutdown with @c atexit so the file sink is closed when the process exits.
         * @param argc The number of arguments
         * @param argv The arguments
         */
        void init(int argc, char* argv[]);

        /**
         * @brief Flushes and closes the file sink. Registered with @c atexit by @ref init; safe to call earlier
         * and more than once. Messages logged afterwards still reach stderr and the ring, but not the file.
         */
        void shutdown();

        /** @brief Flushes every sink. Called after each fatal message. */
        void flush();

        /**
         * @brief Logs a message
         * @param level The severity of the message
         * @param category The category the message belongs to (e.g. "engine", "gpu"); never null
         * @param file The source file the call originated from
         * @param line The source line the call originated from
         * @param format The printf-style format of the message
         * @param ... The arguments
         */
        void message(verbosity level, const char* category, const char* file, unsigned line, const char* format, ...)
            ALPHAENGINE_PRINTF_FORMAT(5, 6);

        /** @brief Sets the global level; messages below it are dropped unless their category overrides it. */
        void set_level(verbosity level);

        /** @brief The global level. */
        verbosity level();

        /** @brief Sets a per-category level that takes precedence over the global one for that category. */
        void set_category_level(const char* category, verbosity level);

        /** @brief Removes every per-category override. */
        void clear_category_levels();

        /** @brief The level in effect for @p category: its override if one is set, else the global level. */
        verbosity level_for(const char* category);

        /** @brief Whether a message at @p level under @p category would currently be emitted. */
        bool is_enabled(verbosity level, const char* category);

        /**
         * @brief Applies a level specification of the form accepted by @c ALPHAENGINE_LOG_LEVEL:
         * @c "<level>" or @c "<level>,<category>=<level>,...", case-insensitive, whitespace ignored; the leading
         * global level may be omitted. Nothing changes if any entry fails to parse.
         * @return true if the whole specification was valid and applied
         */
        bool configure_levels(std::string_view spec);

        /** @brief Parses a level name (@c trace, @c debug, @c info, @c warn, @c error, @c fatal), ignoring case. */
        std::optional<verbosity> parse_verbosity(std::string_view name);

        /** @brief The lowercase name of @p level, the inverse of @ref parse_verbosity. */
        const char* verbosity_name(verbosity level);

        /** @brief A snapshot of the last @ref k_recent_capacity messages, oldest first. Thread-safe. */
        std::vector<record> recent_messages();

        /** @brief Empties the ring returned by @ref recent_messages. */
        void clear_recent_messages();

        /** @brief The command line handed to @ref init (argv[0] included), for a later command-line parser. */
        const std::vector<std::string>& arguments();
    } // namespace logging
} // namespace core
