// Unit tests for util::transform's orientation API in the engine convention
// (+X forward, +Z up): the identity basis, look_at with the default and an
// explicit up, the straight-up / straight-down fallback, the coincident
// target, and that the basis vectors agree with the transform matrix.

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/math.hpp>
#include <rendering_engine/util/transform.hpp>

using core::math::mat4;
using core::math::vec3;
using core::math::vec4;
using rendering_engine::util::transform;

namespace
{
    constexpr float k_eps = 1e-4f;

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

    vec3 column(const mat4& m, int index)
    {
        return vec3{m.m[index * 4], m.m[index * 4 + 1], m.m[index * 4 + 2]};
    }
} // namespace

TEST(transform, identity_faces_plus_x_with_plus_z_up_and_minus_y_right)
{
    transform t;
    expect_vec3_near(t.get_forward(), core::math::world_forward);
    expect_vec3_near(t.get_up(), core::math::world_up);
    expect_vec3_near(t.get_right(), core::math::world_right);
}

TEST(transform, look_at_aims_the_forward_axis_with_plus_z_up)
{
    transform t;
    t.set_position(vec3{1.0f, 2.0f, 3.0f});
    t.look_at(vec3{1.0f, 7.0f, 3.0f});
    expect_vec3_near(t.get_forward(), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(t.get_up(), core::math::world_up);
    expect_vec3_near(t.get_right(), vec3{1.0f, 0.0f, 0.0f});
}

TEST(transform, look_at_honours_an_explicit_up)
{
    transform t;
    t.look_at(vec3{5.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(t.get_forward(), vec3{1.0f, 0.0f, 0.0f});
    expect_vec3_near(t.get_up(), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(t.get_right(), vec3{0.0f, 0.0f, 1.0f});
}

TEST(transform, look_at_straight_down_or_up_yields_a_finite_orthonormal_frame)
{
    transform down;
    down.look_at(vec3{0.0f, 0.0f, -10.0f});
    expect_vec3_near(down.get_forward(), vec3{0.0f, 0.0f, -1.0f});
    EXPECT_TRUE(is_finite(down.get_up()));
    EXPECT_TRUE(is_finite(down.get_right()));
    // The fallback reference up is the engine forward, so the frame's up
    // ends up horizontal along +X.
    expect_vec3_near(down.get_up(), vec3{1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(core::math::dot(down.get_forward(), down.get_up()), 0.0f, k_eps);
    EXPECT_NEAR(core::math::length(down.get_right()), 1.0f, k_eps);

    transform up;
    up.look_at(vec3{0.0f, 0.0f, 10.0f});
    expect_vec3_near(up.get_forward(), vec3{0.0f, 0.0f, 1.0f});
    EXPECT_TRUE(is_finite(up.get_up()));
    EXPECT_NEAR(core::math::dot(up.get_forward(), up.get_up()), 0.0f, k_eps);
}

TEST(transform, look_at_the_current_position_keeps_the_orientation)
{
    transform t;
    t.set_position(vec3{2.0f, 0.0f, 0.0f});
    t.look_at(vec3{2.0f, 5.0f, 0.0f});
    const vec3 before = t.get_forward();
    t.look_at(vec3{2.0f, 0.0f, 0.0f});
    expect_vec3_near(t.get_forward(), before);
}

TEST(transform, basis_vectors_match_the_transform_matrix_columns)
{
    transform t;
    t.set_position(vec3{-3.0f, 4.0f, 1.0f});
    t.look_at(vec3{2.0f, -1.0f, 3.0f});
    const mat4 m = t.get_transform_matrix();
    // Unit scale, so columns 0 / 2 are the forward / up and column 1 is the
    // local +Y, i.e. minus the right.
    expect_vec3_near(column(m, 0), t.get_forward());
    expect_vec3_near(column(m, 1), -t.get_right());
    expect_vec3_near(column(m, 2), t.get_up());
    expect_vec3_near(column(m, 3), vec3{-3.0f, 4.0f, 1.0f});
}

TEST(transform, set_rotation_euler_still_drives_the_basis)
{
    // The euler path stays: a 90 degree yaw about +Z turns the forward to +Y.
    transform t;
    t.set_rotation(vec3{0.0f, 0.0f, 3.14159265f * 0.5f});
    expect_vec3_near(t.get_forward(), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(t.get_up(), core::math::world_up);
}
