// Unit tests for core::math vec2 / vec3 / vec4: arithmetic operators and the
// free functions (dot, cross, normalize, length, distance, lerp).

#include <gtest/gtest.h>

#include <core/math/vec2.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

using namespace core::math;

namespace
{
    constexpr float k_eps = 1e-5f;
}

// -- vec2 -------------------------------------------------------------------

TEST(vec2, default_is_zero)
{
    vec2 v;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
}

TEST(vec2, scalar_constructor_broadcasts)
{
    vec2 v(3.0f);
    EXPECT_FLOAT_EQ(v.x, 3.0f);
    EXPECT_FLOAT_EQ(v.y, 3.0f);
}

TEST(vec2, add_and_subtract)
{
    vec2 a(1.0f, 2.0f);
    vec2 b(4.0f, 6.0f);
    EXPECT_TRUE((a + b) == vec2(5.0f, 8.0f));
    EXPECT_TRUE((b - a) == vec2(3.0f, 4.0f));
}

TEST(vec2, scalar_multiply_is_commutative)
{
    vec2 v(2.0f, -3.0f);
    EXPECT_TRUE(v * 2.0f == 2.0f * v);
    EXPECT_TRUE(v * 2.0f == vec2(4.0f, -6.0f));
}

TEST(vec2, compound_assignment)
{
    vec2 v(1.0f, 1.0f);
    v += vec2(2.0f, 3.0f);
    EXPECT_TRUE(v == vec2(3.0f, 4.0f));
    v -= vec2(1.0f, 1.0f);
    EXPECT_TRUE(v == vec2(2.0f, 3.0f));
    v *= 2.0f;
    EXPECT_TRUE(v == vec2(4.0f, 6.0f));
    v /= 2.0f;
    EXPECT_TRUE(v == vec2(2.0f, 3.0f));
}

TEST(vec2, dot_length_distance)
{
    EXPECT_FLOAT_EQ(dot(vec2(1.0f, 0.0f), vec2(0.0f, 1.0f)), 0.0f);
    EXPECT_FLOAT_EQ(dot(vec2(2.0f, 3.0f), vec2(4.0f, 5.0f)), 23.0f);
    EXPECT_FLOAT_EQ(length(vec2(3.0f, 4.0f)), 5.0f);
    EXPECT_FLOAT_EQ(distance(vec2(1.0f, 1.0f), vec2(4.0f, 5.0f)), 5.0f);
}

TEST(vec2, normalize_yields_unit_length)
{
    vec2 n = normalize(vec2(3.0f, 4.0f));
    EXPECT_NEAR(length(n), 1.0f, k_eps);
    EXPECT_NEAR(n.x, 0.6f, k_eps);
    EXPECT_NEAR(n.y, 0.8f, k_eps);
}

TEST(vec2, lerp_endpoints_and_midpoint)
{
    vec2 a(0.0f, 0.0f);
    vec2 b(10.0f, 20.0f);
    EXPECT_TRUE(lerp(a, b, 0.0f) == a);
    EXPECT_TRUE(lerp(a, b, 1.0f) == b);
    vec2 mid = lerp(a, b, 0.5f);
    EXPECT_NEAR(mid.x, 5.0f, k_eps);
    EXPECT_NEAR(mid.y, 10.0f, k_eps);
}

// -- vec3 -------------------------------------------------------------------

TEST(vec3, dot_and_length)
{
    EXPECT_FLOAT_EQ(dot(vec3(1.0f, 2.0f, 3.0f), vec3(4.0f, 5.0f, 6.0f)), 32.0f);
    EXPECT_FLOAT_EQ(length(vec3(2.0f, 3.0f, 6.0f)), 7.0f);
}

TEST(vec3, cross_is_right_handed)
{
    vec3 x(1.0f, 0.0f, 0.0f);
    vec3 y(0.0f, 1.0f, 0.0f);
    EXPECT_TRUE(cross(x, y) == vec3(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(cross(y, x) == vec3(0.0f, 0.0f, -1.0f));
}

TEST(vec3, cross_is_anticommutative)
{
    vec3 a(1.0f, 2.0f, 3.0f);
    vec3 b(4.0f, 5.0f, 6.0f);
    vec3 ab = cross(a, b);
    vec3 ba = cross(b, a);
    EXPECT_NEAR(ab.x, -ba.x, k_eps);
    EXPECT_NEAR(ab.y, -ba.y, k_eps);
    EXPECT_NEAR(ab.z, -ba.z, k_eps);
}

TEST(vec3, cross_is_orthogonal_to_inputs)
{
    vec3 a(1.0f, 2.0f, 3.0f);
    vec3 b(-2.0f, 0.5f, 4.0f);
    vec3 c = cross(a, b);
    EXPECT_NEAR(dot(c, a), 0.0f, 1e-4f);
    EXPECT_NEAR(dot(c, b), 0.0f, 1e-4f);
}

TEST(vec3, negate)
{
    EXPECT_TRUE(-vec3(1.0f, -2.0f, 3.0f) == vec3(-1.0f, 2.0f, -3.0f));
}

TEST(vec3, normalize_yields_unit_length)
{
    vec3 n = normalize(vec3(0.0f, 3.0f, 4.0f));
    EXPECT_NEAR(length(n), 1.0f, k_eps);
}

// -- vec4 -------------------------------------------------------------------

TEST(vec4, dot_and_length)
{
    EXPECT_FLOAT_EQ(dot(vec4(1.0f, 2.0f, 3.0f, 4.0f), vec4(5.0f, 6.0f, 7.0f, 8.0f)), 70.0f);
    EXPECT_FLOAT_EQ(length(vec4(2.0f, 0.0f, 0.0f, 0.0f)), 2.0f);
}

TEST(vec4, construct_from_vec3_and_w)
{
    vec4 v(vec3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_TRUE(v == vec4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST(vec4, lerp_midpoint)
{
    vec4 mid = lerp(vec4(0.0f, 0.0f, 0.0f, 0.0f), vec4(2.0f, 4.0f, 6.0f, 8.0f), 0.5f);
    EXPECT_TRUE(mid == vec4(1.0f, 2.0f, 3.0f, 4.0f));
}
