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

#define LOGURU_IMPLEMENTATION 1
#include <Loguru.hpp>
#include <Infrastructure/log.hpp>
#include <Infrastructure/Version.hpp>

static int verbosity_to_loguru(infrastructure::verbosity verbosity)
{
    switch (verbosity)
    {
    case infrastructure::verbosity::INFO:
        return loguru::Verbosity_INFO;
    case infrastructure::verbosity::WARN:
        return loguru::Verbosity_WARNING;
    case infrastructure::verbosity::ERROR:
        return loguru::Verbosity_ERROR;
    case infrastructure::verbosity::FATAL:
        return loguru::Verbosity_FATAL;
    default:
        return loguru::Verbosity_OFF;
    }
}

void infrastructure::logging::init(int argc, char* argv[])
{
    loguru::init(argc, argv);

#ifdef _DEBUG
    //loguru::add_file("everything.log", loguru::Append, loguru::Verbosity_MAX);
    //loguru::add_file("latest_readable.log", loguru::Truncate, loguru::Verbosity_INFO);
#else
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::g_colorlogtostderr = false;
#endif

    LOG_INF("AlphaEngine v%s starting ...", Infrastructure::Version::GetVersion().c_str());
}

void infrastructure::logging::message(enum verbosity verbosity, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    VLOG_F(verbosity_to_loguru(verbosity), format, args);
    va_end(args);
}
