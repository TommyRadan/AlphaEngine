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

#pragma once

#define LOG_INIT(argc, argv) infrastructure::logging::init(argc, argv)

#define LOG_INF(...) infrastructure::logging::message(infrastructure::logging::verbosity::INFO, __VA_ARGS__)
#define LOG_WRN(...) infrastructure::logging::message(infrastructure::logging::verbosity::WARN, __VA_ARGS__)
#define LOG_ERR(...) infrastructure::logging::message(infrastructure::logging::verbosity::ERROR, __VA_ARGS__)
#define LOG_FTL(...) infrastructure::logging::message(infrastructure::logging::verbosity::FATAL, __VA_ARGS__)

namespace infrastructure
{
    /**
     * @brief The logging system
     */
    namespace logging
    {
        enum class verbosity
        {
            INFO,
            WARN,
            ERROR,
            FATAL
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
         * @param format The format of the message
         * @param ... The arguments
         */
        void message(enum verbosity verbosity, const char* format, ...);
    };
}
