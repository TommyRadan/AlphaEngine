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

/**
 * @file version.hpp
 * @brief Compile-time version information for the engine.
 */

#pragma once

#include <string>

namespace infrastructure
{
    /**
     * @brief Static accessors for build-time version metadata.
     *
     * The version triplet is baked in from the @c VERSION_MAJOR /
     * @c VERSION_MINOR / @c VERSION_PATCH preprocessor macros (set by
     * CMake), and the build date is captured from @c __DATE__.
     */
    struct version
    {
        /** @brief Returns the version string in @c "MAJOR.MINOR.PATCH" form. */
        static const std::string get_version();

        /** @brief Returns the build date captured at compile time. */
        static const std::string get_build_date();
    };
} // namespace infrastructure