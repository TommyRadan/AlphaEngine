// Unit tests for the engine's coordinate conventions in core/math: the world
// axes (+Z up, +X forward, right-handed), reference_up's choice of a usable
// up around any direction (including one along the up axis, where the
// fallback must be horizontal and never degenerate), quat_from_basis, and
// quat_look_at building orientations in that convention.

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/math.hpp>

using namespace core::math;

namespace
{
    constexpr float k_eps = 1e-4f;
    constexpr float k_pi = 3.14159265358979323846f;

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    bool is_finite(const vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
} // namespace

TEST(conventions, world_axes_form_a_right_handed_z_up_frame)
{
    expect_vec3_near(world_up, vec3{0.0f, 0.0f, 1.0f});
    expect_vec3_near(world_forward, vec3{1.0f, 0.0f, 0.0f});
    // forward x up = right, as for any right-handed (forward, right, up) frame.
    expect_vec3_near(cross(world_forward, world_up), world_right);
    EXPECT_NEAR(dot(world_forward, world_up), 0.0f, k_eps);
    EXPECT_NEAR(dot(world_right, world_up), 0.0f, k_eps);
}

TEST(reference_up, keeps_the_given_up_when_it_is_not_parallel)
{
    expect_vec3_near(reference_up(vec3{1.0f, 0.0f, 0.0f}), world_up);
    expect_vec3_near(reference_up(vec3{0.3f, -0.8f, 0.5f}), world_up);
    // A non-default, non-unit up is returned as given.
    expect_vec3_near(reference_up(vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 2.0f, 0.0f}), vec3{0.0f, 2.0f, 0.0f});
}

TEST(reference_up, falls_back_to_a_horizontal_axis_along_the_up_axis)
{
    // Straight up or down: the engine forward is the substitute.
    expect_vec3_near(reference_up(vec3{0.0f, 0.0f, 1.0f}), world_forward);
    expect_vec3_near(reference_up(vec3{0.0f, 0.0f, -7.0f}), world_forward);
    // Nearly parallel counts as parallel, so the cross product is never tiny.
    const vec3 nearly_down{1e-5f, 0.0f, -1.0f};
    expect_vec3_near(reference_up(nearly_down), world_forward);
    EXPECT_GT(length(cross(nearly_down, reference_up(nearly_down))), 0.5f);
}

TEST(reference_up, picks_the_least_aligned_axis_when_the_forward_is_parallel_too)
{
    // A caller asking for an up along +X while looking along +X gets +Y.
    expect_vec3_near(reference_up(vec3{1.0f, 0.0f, 0.0f}, vec3{1.0f, 0.0f, 0.0f}), vec3{0.0f, 1.0f, 0.0f});
    // A diagonal direction with its own up parallel to it: whichever axis it
    // leans on least, and never something parallel.
    const vec3 diagonal = normalize(vec3{1.0f, 0.2f, 1.0f});
    const vec3 up = reference_up(diagonal, diagonal);
    EXPECT_GT(length(cross(diagonal, up)), 0.5f);
}

TEST(reference_up, a_zero_direction_or_up_never_yields_a_zero_vector)
{
    expect_vec3_near(reference_up(vec3{}, world_up), world_up);
    expect_vec3_near(reference_up(vec3{}, vec3{}), world_up);
    // A zero up with a real direction takes the fallback.
    const vec3 up = reference_up(vec3{0.0f, 1.0f, 0.0f}, vec3{});
    EXPECT_TRUE(is_finite(up));
    EXPECT_GT(length(cross(vec3{0.0f, 1.0f, 0.0f}, up)), 0.5f);
}

TEST(quat_from_basis, identity_axes_give_the_identity_rotation)
{
    const quat q = quat_from_basis(vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f});
    expect_vec3_near(q * vec3{1.0f, 2.0f, 3.0f}, vec3{1.0f, 2.0f, 3.0f});
}

TEST(quat_from_basis, maps_each_local_axis_onto_the_given_world_axis)
{
    // A 90 degree turn about +Z: local X lands on +Y, local Y on -X.
    const vec3 x_axis{0.0f, 1.0f, 0.0f};
    const vec3 y_axis{-1.0f, 0.0f, 0.0f};
    const vec3 z_axis{0.0f, 0.0f, 1.0f};
    const quat q = quat_from_basis(x_axis, y_axis, z_axis);
    expect_vec3_near(q * vec3{1.0f, 0.0f, 0.0f}, x_axis);
    expect_vec3_near(q * vec3{0.0f, 1.0f, 0.0f}, y_axis);
    expect_vec3_near(q * vec3{0.0f, 0.0f, 1.0f}, z_axis);
    // ...and agrees with the euler construction of the same turn.
    const quat via_euler = quat_from_euler(vec3{0.0f, 0.0f, k_pi * 0.5f});
    expect_vec3_near(q * vec3{1.0f, 2.0f, 3.0f}, via_euler * vec3{1.0f, 2.0f, 3.0f});
}

TEST(quat_look_at, along_the_forward_axis_is_the_identity)
{
    const quat q = quat_look_at(world_forward, world_up);
    expect_vec3_near(q * world_forward, world_forward);
    expect_vec3_near(q * world_up, world_up);
    expect_vec3_near(q * world_right, world_right);
}

TEST(quat_look_at, aims_plus_x_at_the_direction_and_keeps_plus_z_up)
{
    // Look along +Y: a 90 degree yaw about +Z, so the right side (-Y) turns to +X.
    const quat q = quat_look_at(vec3{0.0f, 5.0f, 0.0f}, world_up);
    expect_vec3_near(q * world_forward, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(q * world_up, world_up);
    expect_vec3_near(q * world_right, vec3{1.0f, 0.0f, 0.0f});

    // A pitched direction keeps the up in the vertical plane of the direction.
    const vec3 direction = normalize(vec3{1.0f, 0.0f, 1.0f});
    const quat pitched = quat_look_at(direction, world_up);
    expect_vec3_near(pitched * world_forward, direction);
    expect_vec3_near(pitched * world_up, normalize(vec3{-1.0f, 0.0f, 1.0f}));
    expect_vec3_near(pitched * world_right, world_right);
}

TEST(quat_look_at, accepts_non_unit_inputs)
{
    const quat q = quat_look_at(vec3{0.0f, -3.0f, 0.0f}, vec3{0.0f, 0.0f, 9.0f});
    expect_vec3_near(q * world_forward, vec3{0.0f, -1.0f, 0.0f});
    expect_vec3_near(q * world_up, world_up);
    const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    EXPECT_NEAR(len, 1.0f, k_eps);
}
