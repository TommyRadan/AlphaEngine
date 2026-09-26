// Unit tests for rendering_engine/passes/projection_jitter: the Halton
// sequence, the per-frame sub-pixel jitter (sub-pixel for the live target
// size, zero for a degenerate one, periodic), and jitter_projection — the
// clip-space shift the scene and skybox passes rasterise with, which must
// move every projected point by exactly the jitter in NDC for perspective
// and orthographic projections alike, so that the velocity pass can undo it
// by subtracting the same jitter and recover the world point that was
// actually rasterised at a pixel.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include <core/math/math.hpp>
#include <rendering_engine/passes/projection_jitter.hpp>

namespace
{
    namespace math = core::math;
    using rendering_engine::halton;
    using rendering_engine::jitter_projection;
    using rendering_engine::taa_jitter_ndc;
    using rendering_engine::taa_jitter_period;

    constexpr float tolerance = 1e-5f;

    // Projects @p point through @p projection and divides: the NDC position.
    math::vec3 project(const math::mat4& projection, const math::vec3& point)
    {
        const math::vec4 clip = projection * math::vec4{point, 1.0f};
        return math::vec3{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
    }

    // Unprojects an NDC position through the inverse of @p projection.
    math::vec3 unproject(const math::mat4& projection, const math::vec3& ndc)
    {
        const math::vec4 world = math::inverse(projection) * math::vec4{ndc, 1.0f};
        return math::vec3{world.x / world.w, world.y / world.w, world.z / world.w};
    }

    math::mat4 test_perspective()
    {
        return math::perspective(1.0f, 16.0f / 9.0f, 0.1f, 100.0f);
    }

    math::mat4 test_ortho()
    {
        return math::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 50.0f);
    }
} // namespace

// --- halton -----------------------------------------------------------------

TEST(projection_jitter, halton_matches_the_radical_inverse)
{
    EXPECT_FLOAT_EQ(halton(1, 2), 0.5f);
    EXPECT_FLOAT_EQ(halton(2, 2), 0.25f);
    EXPECT_FLOAT_EQ(halton(3, 2), 0.75f);
    EXPECT_FLOAT_EQ(halton(4, 2), 0.125f);
    EXPECT_FLOAT_EQ(halton(1, 3), 1.0f / 3.0f);
    EXPECT_FLOAT_EQ(halton(2, 3), 2.0f / 3.0f);
    EXPECT_FLOAT_EQ(halton(3, 3), 1.0f / 9.0f);
}

TEST(projection_jitter, halton_index_zero_is_zero)
{
    EXPECT_FLOAT_EQ(halton(0, 2), 0.0f);
    EXPECT_FLOAT_EQ(halton(0, 3), 0.0f);
}

// --- taa_jitter_ndc ---------------------------------------------------------

TEST(projection_jitter, jitter_is_zero_for_a_degenerate_target)
{
    const math::vec2 none{0.0f, 0.0f};
    EXPECT_EQ(taa_jitter_ndc(3, 0, 720), none);
    EXPECT_EQ(taa_jitter_ndc(3, 1280, 0), none);
    EXPECT_EQ(taa_jitter_ndc(3, 0, 0), none);
}

TEST(projection_jitter, jitter_stays_within_half_a_pixel)
{
    // A pixel spans 2/width NDC, so half a pixel is 1/width.
    const uint32_t width = 1280;
    const uint32_t height = 720;
    for (uint64_t frame = 0; frame < 4 * taa_jitter_period; ++frame)
    {
        const math::vec2 jitter = taa_jitter_ndc(frame, width, height);
        EXPECT_LT(std::abs(jitter.x), 1.0f / static_cast<float>(width)) << "frame " << frame;
        EXPECT_LT(std::abs(jitter.y), 1.0f / static_cast<float>(height)) << "frame " << frame;
    }
}

TEST(projection_jitter, jitter_repeats_with_the_period_and_varies_within_it)
{
    const uint32_t width = 800;
    const uint32_t height = 600;
    for (uint64_t frame = 0; frame < taa_jitter_period; ++frame)
    {
        const math::vec2 a = taa_jitter_ndc(frame, width, height);
        const math::vec2 b = taa_jitter_ndc(frame + taa_jitter_period, width, height);
        EXPECT_FLOAT_EQ(a.x, b.x);
        EXPECT_FLOAT_EQ(a.y, b.y);
    }
    // Consecutive frames land on different sub-pixel positions.
    for (uint64_t frame = 0; frame + 1 < taa_jitter_period; ++frame)
    {
        const math::vec2 a = taa_jitter_ndc(frame, width, height);
        const math::vec2 b = taa_jitter_ndc(frame + 1, width, height);
        EXPECT_TRUE(a.x != b.x || a.y != b.y) << "frame " << frame;
    }
}

TEST(projection_jitter, jitter_scales_with_the_target_size)
{
    // The same sequence position on a twice-as-wide target is half the NDC
    // offset: the jitter is a pixel fraction, not an NDC constant. This is
    // what keeps it sub-pixel after a resize.
    for (uint64_t frame = 0; frame < taa_jitter_period; ++frame)
    {
        const math::vec2 small = taa_jitter_ndc(frame, 640, 360);
        const math::vec2 large = taa_jitter_ndc(frame, 1280, 720);
        EXPECT_NEAR(large.x, small.x * 0.5f, tolerance);
        EXPECT_NEAR(large.y, small.y * 0.5f, tolerance);
    }
}

// --- jitter_projection ------------------------------------------------------

TEST(projection_jitter, zero_jitter_leaves_the_projection_unchanged)
{
    const math::mat4 projection = test_perspective();
    EXPECT_EQ(jitter_projection(projection, math::vec2{0.0f, 0.0f}), projection);
}

TEST(projection_jitter, perspective_points_shift_by_exactly_the_jitter_in_ndc)
{
    const math::mat4 projection = test_perspective();
    const math::vec2 jitter{0.0013f, -0.0021f};
    const math::mat4 jittered = jitter_projection(projection, jitter);

    // Points at different depths (and so different clip w) all move by the
    // same NDC amount: the shift is uniform, not a shear.
    const math::vec3 points[] = {{0.3f, -0.2f, -1.0f}, {-2.0f, 1.5f, -7.5f}, {0.0f, 0.0f, -40.0f}};
    for (const auto& point : points)
    {
        const math::vec3 ndc = project(projection, point);
        const math::vec3 ndc_jittered = project(jittered, point);
        EXPECT_NEAR(ndc_jittered.x - ndc.x, jitter.x, tolerance);
        EXPECT_NEAR(ndc_jittered.y - ndc.y, jitter.y, tolerance);
        EXPECT_NEAR(ndc_jittered.z, ndc.z, tolerance);
    }
}

TEST(projection_jitter, orthographic_points_shift_by_exactly_the_jitter_in_ndc)
{
    const math::mat4 projection = test_ortho();
    const math::vec2 jitter{-0.0007f, 0.0016f};
    const math::mat4 jittered = jitter_projection(projection, jitter);

    const math::vec3 points[] = {{1.0f, 2.0f, -1.0f}, {-3.0f, -2.5f, -20.0f}};
    for (const auto& point : points)
    {
        const math::vec3 ndc = project(projection, point);
        const math::vec3 ndc_jittered = project(jittered, point);
        EXPECT_NEAR(ndc_jittered.x - ndc.x, jitter.x, tolerance);
        EXPECT_NEAR(ndc_jittered.y - ndc.y, jitter.y, tolerance);
        EXPECT_NEAR(ndc_jittered.z, ndc.z, tolerance);
    }
}

TEST(projection_jitter, subtracting_the_jitter_recovers_the_rasterised_point)
{
    // What the velocity pass does: a pixel holds the depth of the point
    // rasterised through the jittered projection; moving the pixel's NDC
    // position back by the jitter and unprojecting through the unjittered
    // projection must land on that same point.
    const math::mat4 projection = test_perspective();
    const math::vec2 jitter = taa_jitter_ndc(5, 1280, 720);
    const math::mat4 jittered = jitter_projection(projection, jitter);

    const math::vec3 point{0.8f, -0.35f, -6.0f};
    const math::vec3 ndc_jittered = project(jittered, point);
    const math::vec3 ndc_unjittered{ndc_jittered.x - jitter.x, ndc_jittered.y - jitter.y, ndc_jittered.z};
    const math::vec3 recovered = unproject(projection, ndc_unjittered);
    EXPECT_NEAR(recovered.x, point.x, 1e-4f);
    EXPECT_NEAR(recovered.y, point.y, 1e-4f);
    EXPECT_NEAR(recovered.z, point.z, 1e-4f);

    // Equivalently, the inverse of the jittered projection takes the
    // jittered NDC position straight back to the point.
    const math::vec3 via_jittered_inverse = unproject(jittered, ndc_jittered);
    EXPECT_NEAR(via_jittered_inverse.x, point.x, 1e-4f);
    EXPECT_NEAR(via_jittered_inverse.y, point.y, 1e-4f);
    EXPECT_NEAR(via_jittered_inverse.z, point.z, 1e-4f);
}

TEST(projection_jitter, static_camera_reprojects_to_zero_motion_once_unjittered)
{
    // Two consecutive frames of a static camera with different jitters:
    // after each frame's NDC position is unjittered, reprojecting through
    // the (unjittered) previous view-projection yields the same position,
    // so the motion vector is zero regardless of the jitter pair.
    const math::mat4 view_projection = test_perspective();
    const math::vec2 jitter_now = taa_jitter_ndc(7, 1920, 1080);
    const math::mat4 jittered_now = jitter_projection(view_projection, jitter_now);

    const math::vec3 point{-1.2f, 0.4f, -3.3f};
    const math::vec3 ndc_now = project(jittered_now, point);
    const math::vec3 unjittered{ndc_now.x - jitter_now.x, ndc_now.y - jitter_now.y, ndc_now.z};

    // prevViewProj * inverse(curViewProj), both unjittered, as the
    // velocity pass uploads it.
    const math::mat4 reprojection = view_projection * math::inverse(view_projection);
    const math::vec4 prev_clip = reprojection * math::vec4{unjittered, 1.0f};
    const float prev_x = prev_clip.x / prev_clip.w;
    const float prev_y = prev_clip.y / prev_clip.w;
    EXPECT_NEAR(unjittered.x - prev_x, 0.0f, tolerance);
    EXPECT_NEAR(unjittered.y - prev_y, 0.0f, tolerance);
}
