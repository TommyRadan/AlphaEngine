// Unit tests for drawable_aspect_ratio, the pure helper the perspective
// camera constructor and the renderer's resize path share to turn a
// drawable's pixel size into a projection aspect: width over height for a
// real drawable, the caller's fallback when either dimension is zero (no
// window yet, or a minimised one) so a degenerate size never divides by
// zero or produces a zero aspect.

#include <gtest/gtest.h>

#include <cstdint>

#include <rendering_engine/camera/perspective_camera.hpp>

namespace
{
    using rendering_engine::drawable_aspect_ratio;

    TEST(drawable_aspect_ratio, is_width_over_height_for_a_real_drawable)
    {
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(1920u, 1080u, 1.0f), 1920.0f / 1080.0f);
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(800u, 600u, 1.0f), 4.0f / 3.0f);
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(1080u, 1920u, 1.0f), 1080.0f / 1920.0f);
    }

    TEST(drawable_aspect_ratio, square_drawable_is_one)
    {
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(512u, 512u, 0.5f), 1.0f);
    }

    TEST(drawable_aspect_ratio, zero_dimension_yields_the_fallback)
    {
        const float fallback = 16.0f / 9.0f;
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(0u, 0u, fallback), fallback);
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(0u, 1080u, fallback), fallback);
        EXPECT_FLOAT_EQ(drawable_aspect_ratio(1920u, 0u, fallback), fallback);
    }

    TEST(drawable_aspect_ratio, is_usable_in_constant_expressions)
    {
        constexpr float aspect = drawable_aspect_ratio(200u, 100u, 1.0f);
        static_assert(aspect == 2.0f, "drawable_aspect_ratio must be constexpr");
        constexpr float degenerate = drawable_aspect_ratio(0u, 100u, 3.0f);
        static_assert(degenerate == 3.0f, "a zero dimension must yield the fallback");
        EXPECT_FLOAT_EQ(aspect, 2.0f);
    }
} // namespace
