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
 * @brief Printf-style logging facade backed by Loguru.
 *
 * Prefer the @c LOG_INF / @c LOG_WRN / @c LOG_ERR / @c LOG_FTL macros
 * over calling @ref infrastructure::logging::message directly — they
 * capture the originating @c __FILE__ and @c __LINE__ automatically.
 */

#pragma once

/** @brief Initializes the logging system. Wraps @ref infrastructure::logging::init. */
#define LOG_INIT(argc, argv) infrastructure::logging::init(argc, argv)

/** @brief Logs an informational message (printf-style). */
#define LOG_INF(...)                                                                                                   \
    infrastructure::logging::message(infrastructure::logging::verbosity::info, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs a warning message (printf-style). */
#define LOG_WRN(...)                                                                                                   \
    infrastructure::logging::message(infrastructure::logging::verbosity::warn, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs an error message (printf-style). */
#define LOG_ERR(...)                                                                                                   \
    infrastructure::logging::message(infrastructure::logging::verbosity::error, __FILE__, __LINE__, __VA_ARGS__)
/** @brief Logs a fatal message (printf-style); Loguru will abort the process. */
#define LOG_FTL(...)                                                                                                   \
    infrastructure::logging::message(infrastructure::logging::verbosity::fatal, __FILE__, __LINE__, __VA_ARGS__)

namespace infrastructure
{
    /**
     * @brief The logging system
     */
    namespace logging
    {
        /** @brief Severity levels accepted by @ref message. */
        enum class verbosity
        {
            info,
            warn,
            error,
            fatal
        };

        /**
         * @brief Initializes the logging system
         * @param argc The number of arguments
         * @param argv The arguments
         */
        void init(int argc, char* argv[]);

        /**
         * @brief Logs a message
         * @param verbosity The verbosity of the message (INFO, WARN, ERROR, FATAL)
         * @param file The source file the call originated from
         * @param line The source line the call originated from
         * @param format The format of the message
         * @param ... The arguments
         */
        void message(enum verbosity verbosity, const char* file, unsigned line, const char* format, ...);
    }; // namespace logging
} // namespace infrastructure
