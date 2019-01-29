/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <Loguru.hpp>

#define LOG_INIT(argc, argv) loguru::init(argc, argv)

#define LOG_INFO(...) LOG_F(INFO, __VA_ARGS__)
#define LOG_WARN(...) LOG_F(WARNING, __VA_ARGS__)
#define LOG_ERROR(...) LOG_F(ERROR, __VA_ARGS__)
#define LOG_FATAL(...) LOG_F(FATAL, __VA_ARGS__)

#define LOG_FUNCTION() LOG_SCOPE_FUNCTION(INFO)

namespace Infrastructure
{
    class Log
    {
        Log() = default;

    public:
        static Log* const GetInstance();

        void Init();
    };
}
