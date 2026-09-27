// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <core/settings_parse.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace core
{
    std::string_view trim(std::string_view text)
    {
        const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!text.empty() && is_space(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && is_space(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    bool equals_ignoring_case(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            {
                return false;
            }
        }
        return true;
    }

    std::optional<bool> parse_bool(std::string_view text)
    {
        text = trim(text);
        constexpr std::string_view k_true_words[] = {"true", "1", "yes", "on"};
        constexpr std::string_view k_false_words[] = {"false", "0", "no", "off"};
        for (const std::string_view word : k_true_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return true;
            }
        }
        for (const std::string_view word : k_false_words)
        {
            if (equals_ignoring_case(text, word))
            {
                return false;
            }
        }
        return std::nullopt;
    }

    std::optional<unsigned int> parse_unsigned(std::string_view text, unsigned int min, unsigned int max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        unsigned int value = 0;
        const char* const end = text.data() + text.size();
        // from_chars rejects a leading sign for unsigned targets, and the
        // pointer check rejects trailing characters.
        const auto [ptr, ec] = std::from_chars(text.data(), end, value, 10);
        if (ec != std::errc{} || ptr != end)
        {
            return std::nullopt;
        }
        if (value < min || value > max)
        {
            return std::nullopt;
        }
        return value;
    }

    std::optional<float> parse_float(std::string_view text, float min, float max)
    {
        text = trim(text);
        if (text.empty())
        {
            return std::nullopt;
        }
        // A classic-locale stream rather than strtof so a decimal-comma
        // process locale cannot change what "0.5" means, and rather than
        // from_chars because not every supported standard library ships the
        // floating-point overloads yet.
        std::istringstream stream{std::string{text}};
        stream.imbue(std::locale::classic());
        double value = 0.0;
        stream >> value;
        if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
        {
            return std::nullopt;
        }
        if (!std::isfinite(value) || value < static_cast<double>(min) || value > static_cast<double>(max))
        {
            return std::nullopt;
        }
        return static_cast<float>(value);
    }
} // namespace core
