// Unit tests for core::math quat: identity rotation, normalization, the
// Hamilton product, inverse, euler round-tripping, to_mat4 consistency, and
// quat_look_at (forward lands on the direction, up stays upright).

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/mat4.hpp>
#include <core/math/quat.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

using namespace core::math;

namespace
{
    constexpr float k_eps = 1e-4f;
    constexpr float k_pi = 3.14159265358979323846f;
    constexpr float k_half_pi = k_pi * 0.5f;

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }
}

TEST(quat, default_is_identity_rotation)
{
    quat q;
    vec3 v(1.0f, 2.0f, 3.0f);
    expect_vec3_near(q * v, v);
}

TEST(quat, normalize_yields_unit_quaternion)
{
    quat q = normalize(quat(1.0f, 1.0f, 1.0f, 1.0f));
    float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    EXPECT_NEAR(len, 1.0f, k_eps);
}

TEST(quat, rotation_about_z_maps_x_to_y)
{
    quat q = quat_from_euler(vec3(0.0f, 0.0f, k_half_pi));
    expect_vec3_near(q * vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
}

TEST(quat, rotation_preserves_length)
{
    quat q = quat_from_euler(vec3(0.3f, -0.7f, 1.1f));
    vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_NEAR(length(q * v), length(v), k_eps);
}

TEST(quat, inverse_undoes_rotation)
{
    quat q = normalize(quat_from_euler(vec3(0.4f, 0.5f, -0.6f)));
    quat round_trip = inverse(q) * q;
    vec3 v(1.0f, -2.0f, 0.5f);
    expect_vec3_near(round_trip * v, v);
}

TEST(quat, hamilton_product_composes_rotations)
{
    quat a = quat_from_euler(vec3(0.0f, 0.0f, k_half_pi));
    // Applying the composed rotation twice about z (90 + 90) flips x to -x.
    quat composed = a * a;
    expect_vec3_near(composed * vec3(1.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f));
}

TEST(quat, euler_round_trips_through_quaternion)
{
    vec3 euler(0.2f, -0.4f, 0.5f);
    vec3 recovered = euler_from_quat(quat_from_euler(euler));
    expect_vec3_near(recovered, euler, 1e-3f);
}

TEST(quat, to_mat4_matches_direct_rotation)
{
    quat q = normalize(quat_from_euler(vec3(0.3f, 0.6f, -0.2f)));
    mat4 m = to_mat4(q);
    vec3 v(1.0f, 2.0f, 3.0f);
    vec4 via_matrix = m * vec4(v, 0.0f);
    vec3 via_quat = q * v;
    EXPECT_NEAR(via_matrix.x, via_quat.x, k_eps);
    EXPECT_NEAR(via_matrix.y, via_quat.y, k_eps);
    EXPECT_NEAR(via_matrix.z, via_quat.z, k_eps);
}

// -- quat_look_at -------------------------------------------------------------

TEST(quat, quat_look_at_points_forward_along_the_direction)
{
    // The engine's forward is -Z: the rotation must carry it onto the
    // (normalised) direction, for an axis and for an arbitrary vector.
    const vec3 forward(0.0f, 0.0f, -1.0f);
    const vec3 up(0.0f, 1.0f, 0.0f);

    quat to_x = quat_look_at(vec3(1.0f, 0.0f, 0.0f), up);
    expect_vec3_near(to_x * forward, vec3(1.0f, 0.0f, 0.0f));

    quat to_back = quat_look_at(vec3(0.0f, 0.0f, 1.0f), up);
    expect_vec3_near(to_back * forward, vec3(0.0f, 0.0f, 1.0f));

    const vec3 direction(3.0f, -1.0f, 2.0f); // not unit length: normalised inside
    quat to_direction = quat_look_at(direction, up);
    const float len = std::sqrt(3.0f * 3.0f + 1.0f + 2.0f * 2.0f);
    expect_vec3_near(to_direction * forward, vec3(3.0f / len, -1.0f / len, 2.0f / len));
}

TEST(quat, quat_look_at_yields_a_unit_rotation_that_keeps_up_upright)
{
    const vec3 up(0.0f, 1.0f, 0.0f);
    quat q = quat_look_at(vec3(1.0f, 0.0f, -1.0f), up);

    // Unit quaternion, so it is a pure rotation (lengths preserved).
    EXPECT_NEAR(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z, 1.0f, k_eps);
    EXPECT_NEAR(length(q * vec3(2.0f, 3.0f, 4.0f)), std::sqrt(29.0f), k_eps);

    // With the direction perpendicular to up, up is mapped onto itself and
    // right stays in the horizontal plane (no roll).
    expect_vec3_near(q * up, up);
    const vec3 right = q * vec3(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(right.y, 0.0f, k_eps);
    EXPECT_NEAR(dot(right, q * vec3(0.0f, 0.0f, -1.0f)), 0.0f, k_eps);
}

TEST(quat, quat_look_at_down_the_forward_axis_is_the_identity)
{
    // Looking straight down -Z with +Y up is where the identity already
    // points, so the rotation is the identity (up to the sign of w).
    quat q = quat_look_at(vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(std::abs(q.w), 1.0f, k_eps);
    EXPECT_NEAR(q.x, 0.0f, k_eps);
    EXPECT_NEAR(q.y, 0.0f, k_eps);
    EXPECT_NEAR(q.z, 0.0f, k_eps);
}
