// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file cache_key.hpp
 * @brief Locale-independent, round-trip-exact text for the numbers that go
 *        into an asset cache's structural key.
 */

#pragma once

#include <charconv>
#include <string>
#include <type_traits>

namespace assets
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
} // namespace assets
