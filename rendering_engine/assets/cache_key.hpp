/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
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
 * @file cache_key.hpp
 * @brief Locale-independent, round-trip-exact text for the numbers that go
 *        into an @ref asset_cache structural key.
 */

#pragma once

#include <charconv>
#include <string>
#include <type_traits>

namespace rendering_engine
{
    /**
     * @brief The text a numeric shape parameter contributes to a cache key.
     *
     * `std::to_string(float)` is the wrong tool for a key: it prints six
     * fixed decimals, so `1.0f` and `1.0000001f` collapse to the same entry
     * (two different meshes would share one upload), and it honours the C
     * locale, so a process running under a comma-decimal locale keys
     * `"1,000000"` and never hits an entry another locale wrote. This uses
     * `std::to_chars`, which is locale-independent and prints the shortest
     * digits that round-trip exactly: `1.0f` keys as `"1"`, `1.0000001f`
     * as `"1.0000001"`, and every distinct value gets a distinct key.
     * Integers print as decimal, `bool` as `1` / `0`.
     */
    template<typename T>
    std::string cache_key_number(T value)
    {
        static_assert(std::is_arithmetic_v<T>, "cache_key_number formats arithmetic parameters");
        if constexpr (std::is_same_v<T, bool>)
        {
            return value ? "1" : "0";
        }
        else
        {
            // Long enough for any integer or the shortest round-trip form of a double (at most 24 characters).
            char buffer[32];
            const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return std::string{buffer, result.ptr};
        }
    }
} // namespace rendering_engine
