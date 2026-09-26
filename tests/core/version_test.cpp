// Unit tests for core::version: the version string is the dotted triplet the
// VERSION_MAJOR / VERSION_MINOR / VERSION_PATCH macros define (CMake hands the
// same definitions to this translation unit), and the build date is the
// predefined __DATE__ form ("Mmm dd yyyy"). Both are stable across calls.

#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include <core/version.hpp>

namespace
{
    std::vector<std::string> split(const std::string& text, char separator)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t end = text.find(separator, start);
            if (end == std::string::npos)
            {
                parts.push_back(text.substr(start));
                return parts;
            }
            parts.push_back(text.substr(start, end - start));
            start = end + 1;
        }
    }

    bool all_digits(const std::string& text)
    {
        if (text.empty())
        {
            return false;
        }
        for (const char c : text)
        {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

TEST(version, version_string_is_a_dotted_numeric_triplet)
{
    const std::string version = core::version::get_version();
    const std::vector<std::string> parts = split(version, '.');

    ASSERT_EQ(parts.size(), 3u) << version;
    for (const std::string& part : parts)
    {
        EXPECT_TRUE(all_digits(part)) << version;
    }
}

#if defined(VERSION_MAJOR) && defined(VERSION_MINOR) && defined(VERSION_PATCH)
TEST(version, version_string_matches_the_configured_macros)
{
    // The root CMakeLists.txt defines the triplet for every target, this
    // binary included, so the string must be built from those exact values.
    const std::string expected =
        std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR) + "." + std::to_string(VERSION_PATCH);
    EXPECT_EQ(core::version::get_version(), expected);
}
#endif

TEST(version, version_string_is_stable_across_calls)
{
    EXPECT_EQ(core::version::get_version(), core::version::get_version());
    EXPECT_EQ(core::version::get_build_date(), core::version::get_build_date());
}

TEST(version, build_date_has_the_predefined_date_layout)
{
    // __DATE__ is "Mmm dd yyyy": an abbreviated English month, a space, the
    // day right-aligned in two columns (space-padded below 10), a space and a
    // four-digit year. Eleven characters, always.
    const std::string date = core::version::get_build_date();
    ASSERT_EQ(date.size(), 11u) << date;

    static const char* const k_months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::string month = date.substr(0, 3);
    bool known_month = false;
    for (const char* candidate : k_months)
    {
        known_month = known_month || month == candidate;
    }
    EXPECT_TRUE(known_month) << date;

    EXPECT_EQ(date[3], ' ') << date;
    EXPECT_TRUE(date[4] == ' ' || std::isdigit(static_cast<unsigned char>(date[4])) != 0) << date;
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(date[5])) != 0) << date;
    EXPECT_EQ(date[6], ' ') << date;
    EXPECT_TRUE(all_digits(date.substr(7))) << date;

    const int day = std::stoi(date.substr(4, 2));
    EXPECT_GE(day, 1) << date;
    EXPECT_LE(day, 31) << date;
}
