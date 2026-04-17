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
 * @file buffer.hpp
 * @brief In-memory byte buffer loaded from a file.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace infrastructure
{
    /**
     * @brief Owns the raw bytes of a file read from disk.
     *
     * The contents are read in full during construction and owned by
     * the @c std::vector member — the pointer returned by @ref get_data
     * is valid for the lifetime of the @ref buffer instance.
     */
    struct buffer
    {
        /**
         * @brief Reads @p filename in binary mode into memory.
         *        On failure the buffer is left empty and an error is logged
         *        (the constructor does not throw).
         * @param filename Path to the file to load.
         */
        buffer(const std::string& filename);

        /**
         * @brief Pointer to the loaded bytes, or to empty storage if the
         *        load failed. Valid until the @ref buffer is destroyed.
         */
        const uint8_t* get_data() const;

    private:
        std::vector<uint8_t> m_data;
    };
} // namespace infrastructure
