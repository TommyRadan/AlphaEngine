/**
 * Copyright (c) 2018 Tomislav Radanovic
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

#include <Infrastructure/Version.hpp>

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifndef VERSION_MAJOR
#warning VERSION_MAJOR not defined, setting to default value ...
#define VERSION_MAJOR 0
#endif

#ifndef VERSION_MINOR
#warning VERSION_MINOR not defined, setting to default value ...
#define VERSION_MINOR 0
#endif

#ifndef VERSION_PATCH
#warning VERSION_PATCH not defined, setting to default value ...
#define VERSION_PATCH 0
#endif

const std::string Infrastructure::Version::GetVersion()
{
    return std::string {
        STR(VERSION_MAJOR) +
        std::string { "." } +
        STR(VERSION_MINOR) +
        std::string { "." } +
        STR(VERSION_PATCH)
    };
}

const std::string Infrastructure::Version::GetBuildDate()
{
    return std::string { __DATE__ };
}
