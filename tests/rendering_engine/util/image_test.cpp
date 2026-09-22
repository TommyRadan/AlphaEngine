// Unit tests for rendering_engine::util::image and util::color: the packed
// RGBA8 texel layout every GPU upload site relies on, and the value semantics
// of the pixel buffer — deep copies, copy-and-swap assignment (larger over
// smaller, over an empty image, self-assignment) and moves that leave the
// source empty. Images are built in memory through the (width, height,
// background) constructor, so no file I/O or decoder is involved; the only
// file path exercised is the missing-file failure of the loading constructor.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

namespace
{
    using rendering_engine::util::color;
    using rendering_engine::util::image;

    bool same_color(const color& lhs, const color& rhs)
    {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
    }

    // Every pixel gets a value derived from its coordinates so a deep copy
    // can be told apart from its source after one of them is mutated.
    color gradient_at(uint32_t x, uint32_t y)
    {
        return color{static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(x + y), 255};
    }

    image make_gradient(uint32_t width, uint32_t height)
    {
        image img{width, height, color{0, 0, 0, 0}};
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                img.set_pixel(x, y, gradient_at(x, y));
            }
        }
        return img;
    }

    bool matches_gradient(const image& img, uint32_t width, uint32_t height)
    {
        if (img.get_width() != width || img.get_height() != height || img.get_pixels() == nullptr)
        {
            return false;
        }
        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                if (!same_color(img.get_pixel(x, y), gradient_at(x, y)))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool is_empty(const image& img)
    {
        return img.get_width() == 0 && img.get_height() == 0 && img.get_pixels() == nullptr;
    }
} // namespace

TEST(util_color, is_a_packed_rgba8_texel)
{
    EXPECT_EQ(sizeof(color), 4u);

    // Byte order matters as much as size: pixels go to the GPU as raw bytes.
    const color c{1, 2, 3, 4};
    std::uint8_t bytes[4] = {};
    std::memcpy(bytes, &c, sizeof(bytes));
    EXPECT_EQ(bytes[0], 1);
    EXPECT_EQ(bytes[1], 2);
    EXPECT_EQ(bytes[2], 3);
    EXPECT_EQ(bytes[3], 4);
}

TEST(util_image, default_constructed_is_empty)
{
    image img;
    EXPECT_TRUE(is_empty(img));

    // Out-of-range reads yield a zero color and writes are ignored — including
    // on an image with no storage at all.
    EXPECT_TRUE(same_color(img.get_pixel(0, 0), color{0, 0, 0, 0}));
    img.set_pixel(0, 0, color{9, 9, 9, 9});
    EXPECT_TRUE(is_empty(img));
}

TEST(util_image, background_constructor_fills_every_pixel)
{
    const color background{10, 20, 30, 40};
    const image img{3, 2, background};
    EXPECT_EQ(img.get_width(), 3u);
    EXPECT_EQ(img.get_height(), 2u);
    ASSERT_NE(img.get_pixels(), nullptr);
    for (uint32_t i = 0; i < 6; ++i)
    {
        EXPECT_TRUE(same_color(img.get_pixels()[i], background));
    }
}

TEST(util_image, pixels_are_stored_row_major)
{
    image img{4, 3, color{0, 0, 0, 0}};
    img.set_pixel(1, 2, color{7, 7, 7, 7});
    EXPECT_TRUE(same_color(img.get_pixel(1, 2), color{7, 7, 7, 7}));
    EXPECT_TRUE(same_color(img.get_pixels()[2 * 4 + 1], color{7, 7, 7, 7}));
}

TEST(util_image, out_of_range_access_is_ignored)
{
    image img{2, 2, color{1, 1, 1, 1}};
    img.set_pixel(2, 0, color{9, 9, 9, 9});
    img.set_pixel(0, 2, color{9, 9, 9, 9});
    EXPECT_TRUE(same_color(img.get_pixel(2, 0), color{0, 0, 0, 0}));
    EXPECT_TRUE(same_color(img.get_pixel(0, 2), color{0, 0, 0, 0}));
    for (uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(same_color(img.get_pixels()[i], color{1, 1, 1, 1}));
    }
}

TEST(util_image, copy_construction_is_deep)
{
    const image source = make_gradient(5, 4);
    image copy{source};

    EXPECT_TRUE(matches_gradient(copy, 5, 4));
    EXPECT_NE(copy.get_pixels(), source.get_pixels());

    copy.set_pixel(0, 0, color{200, 200, 200, 200});
    EXPECT_TRUE(matches_gradient(source, 5, 4));
}

TEST(util_image, copying_an_empty_image_stays_empty)
{
    const image source;
    const image copy{source};
    EXPECT_TRUE(is_empty(copy));

    image assigned = make_gradient(2, 2);
    assigned = source;
    EXPECT_TRUE(is_empty(assigned));
}

TEST(util_image, copy_assigning_a_larger_image_over_a_smaller_one)
{
    // Previously memcpy'd into the smaller existing buffer: a heap overflow.
    const image large = make_gradient(16, 8);
    image small{2, 2, color{5, 5, 5, 5}};

    small = large;

    EXPECT_TRUE(matches_gradient(small, 16, 8));
    EXPECT_NE(small.get_pixels(), large.get_pixels());
    EXPECT_TRUE(matches_gradient(large, 16, 8));
}

TEST(util_image, copy_assigning_over_an_empty_image)
{
    // Previously memcpy'd into a null destination.
    const image source = make_gradient(3, 3);
    image target;

    target = source;

    EXPECT_TRUE(matches_gradient(target, 3, 3));
    EXPECT_NE(target.get_pixels(), source.get_pixels());
}

TEST(util_image, self_assignment_keeps_contents)
{
    image img = make_gradient(4, 4);
    image& alias = img;

    img = alias;

    EXPECT_TRUE(matches_gradient(img, 4, 4));
}

TEST(util_image, move_construction_transfers_the_buffer_and_empties_the_source)
{
    image source = make_gradient(6, 2);
    const color* const pixels = source.get_pixels();

    image moved{std::move(source)};

    EXPECT_TRUE(matches_gradient(moved, 6, 2));
    EXPECT_EQ(moved.get_pixels(), pixels);
    EXPECT_TRUE(is_empty(source)); // NOLINT(bugprone-use-after-move): the emptied state is the contract
}

TEST(util_image, move_assignment_replaces_contents_and_empties_the_source)
{
    // Previously overwrote the pointer without freeing the old allocation.
    image source = make_gradient(6, 2);
    const color* const pixels = source.get_pixels();
    image target = make_gradient(3, 3);

    target = std::move(source);

    EXPECT_TRUE(matches_gradient(target, 6, 2));
    EXPECT_EQ(target.get_pixels(), pixels);
    EXPECT_TRUE(is_empty(source)); // NOLINT(bugprone-use-after-move): the emptied state is the contract
}

TEST(util_image, swap_exchanges_contents)
{
    image a = make_gradient(2, 3);
    image b{1, 1, color{1, 2, 3, 4}};

    using std::swap;
    swap(a, b);

    EXPECT_EQ(a.get_width(), 1u);
    EXPECT_EQ(a.get_height(), 1u);
    EXPECT_TRUE(same_color(a.get_pixel(0, 0), color{1, 2, 3, 4}));
    EXPECT_TRUE(matches_gradient(b, 2, 3));
}

TEST(util_image, adopting_constructor_takes_ownership_of_the_buffer)
{
    color* const buffer = new color[4];
    for (int i = 0; i < 4; ++i)
    {
        buffer[i] = color{static_cast<uint8_t>(i), 0, 0, 255};
    }

    // Freed by the image's destructor; the test leaks nothing under a sanitizer.
    const image img{2, 2, buffer};
    EXPECT_EQ(img.get_pixels(), buffer);
    EXPECT_TRUE(same_color(img.get_pixel(1, 1), color{3, 0, 0, 255}));
}

TEST(util_image, loading_a_missing_file_throws)
{
    EXPECT_THROW(image{"/nonexistent/alpha_engine_image_test_missing.png"}, std::runtime_error);
}
