// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file settings_parse.hpp
 * @brief Pure text parsers shared by @ref core::settings_registry and by every module's own `choice`-field
 *        parser (a `parse_window_mode`-shaped function that recognises its own enum's names).
 *
 * Every parser is lenient in the same way: input is trimmed, names are matched ignoring case, and an
 * unrecognised or out-of-range value simply fails to parse (returns an empty @c std::optional) — the caller
 * decides how to warn and what to keep.
 */

#pragma once

#include <optional>
#include <string_view>

namespace core
{
    /** @brief @p text with leading and trailing whitespace removed. */
    std::string_view trim(std::string_view text);

    /** @brief Whether @p a and @p b are equal, ignoring ASCII case. */
    bool equals_ignoring_case(std::string_view a, std::string_view b);

    /** @brief Parses `true` / `false`, `1` / `0`, `yes` / `no`, `on` / `off`; trimmed, case-insensitive. */
    std::optional<bool> parse_bool(std::string_view text);

    /** @brief Parses a decimal unsigned integer in `[min, max]`; a sign or trailing characters reject it. */
    std::optional<unsigned int> parse_unsigned(std::string_view text, unsigned int min, unsigned int max);

    /** @brief Parses a finite decimal number in `[min, max]` in the C locale; trailing characters reject it. */
    std::optional<float> parse_float(std::string_view text, float min, float max);
} // namespace core
