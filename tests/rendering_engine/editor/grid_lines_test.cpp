// Unit tests for rendering_engine/editor/grid_lines.hpp: the line layout of
// the finite grid_helper. An even division count has a spaced line through
// the origin, which is the accented centre line; an odd count puts the origin
// mid-cell, so a centre line is added at 0 (and the lines stay in ascending
// order). Header-only, so no engine translation unit is compiled for it.

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include <rendering_engine/editor/grid_lines.hpp>

namespace
{
    using rendering_engine::editor::grid_line;
    using rendering_engine::editor::grid_lines;

    std::size_t center_count(const std::vector<grid_line>& lines)
    {
        std::size_t count = 0;
        for (const auto& line : lines)
        {
            if (line.center)
            {
                ++count;
            }
        }
        return count;
    }

    void expect_ascending(const std::vector<grid_line>& lines)
    {
        for (std::size_t i = 1; i < lines.size(); ++i)
        {
            EXPECT_LT(lines[i - 1].coord, lines[i].coord) << "index " << i;
        }
    }
} // namespace

TEST(grid_lines, even_divisions_accent_the_spaced_line_through_the_origin)
{
    const auto lines = grid_lines(10.0f, 10);
    ASSERT_EQ(lines.size(), 11u);
    expect_ascending(lines);
    EXPECT_EQ(center_count(lines), 1u);
    for (int i = 0; i <= 10; ++i)
    {
        EXPECT_FLOAT_EQ(lines[static_cast<std::size_t>(i)].coord, -5.0f + static_cast<float>(i));
        EXPECT_EQ(lines[static_cast<std::size_t>(i)].center, i == 5) << "index " << i;
    }
    EXPECT_FLOAT_EQ(lines[5].coord, 0.0f);
}

TEST(grid_lines, odd_divisions_insert_a_centre_line_at_the_origin)
{
    // Five cells across ten units: spaced lines at -5, -3, -1, 1, 3, 5,
    // none through the origin, so a seventh line is added at 0.
    const auto lines = grid_lines(10.0f, 5);
    ASSERT_EQ(lines.size(), 7u);
    expect_ascending(lines);
    const float expected[] = {-5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 3.0f, 5.0f};
    for (std::size_t i = 0; i < 7; ++i)
    {
        EXPECT_FLOAT_EQ(lines[i].coord, expected[i]) << "index " << i;
        EXPECT_EQ(lines[i].center, i == 3) << "index " << i;
    }
    EXPECT_EQ(center_count(lines), 1u);
}

TEST(grid_lines, the_centre_line_is_exactly_at_zero_for_any_even_count)
{
    // Computed from the index rather than the float sum, so the origin is
    // hit exactly even when size / divisions is not representable.
    for (int divisions = 2; divisions <= 64; divisions += 2)
    {
        const auto lines = grid_lines(7.3f, divisions);
        ASSERT_EQ(lines.size(), static_cast<std::size_t>(divisions) + 1u);
        const auto& center = lines[static_cast<std::size_t>(divisions / 2)];
        EXPECT_TRUE(center.center) << "divisions " << divisions;
        EXPECT_EQ(center.coord, 0.0f) << "divisions " << divisions;
        EXPECT_EQ(center_count(lines), 1u) << "divisions " << divisions;
    }
}

TEST(grid_lines, every_odd_count_gets_exactly_one_centre_line_in_order)
{
    for (int divisions = 1; divisions <= 63; divisions += 2)
    {
        const auto lines = grid_lines(7.3f, divisions);
        ASSERT_EQ(lines.size(), static_cast<std::size_t>(divisions) + 2u) << "divisions " << divisions;
        expect_ascending(lines);
        EXPECT_EQ(center_count(lines), 1u) << "divisions " << divisions;
        const auto& center = lines[static_cast<std::size_t>((divisions + 1) / 2)];
        EXPECT_TRUE(center.center) << "divisions " << divisions;
        EXPECT_EQ(center.coord, 0.0f) << "divisions " << divisions;
    }
}

TEST(grid_lines, a_single_cell_has_its_edges_and_a_centre_line)
{
    const auto lines = grid_lines(4.0f, 1);
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_FLOAT_EQ(lines[0].coord, -2.0f);
    EXPECT_FLOAT_EQ(lines[1].coord, 0.0f);
    EXPECT_FLOAT_EQ(lines[2].coord, 2.0f);
    EXPECT_FALSE(lines[0].center);
    EXPECT_TRUE(lines[1].center);
    EXPECT_FALSE(lines[2].center);
}

TEST(grid_lines, divisions_below_one_are_treated_as_one)
{
    EXPECT_EQ(grid_lines(4.0f, 0).size(), 3u);
    EXPECT_EQ(grid_lines(4.0f, -7).size(), 3u);
    EXPECT_FLOAT_EQ(grid_lines(4.0f, 0)[2].coord, 2.0f);
}
