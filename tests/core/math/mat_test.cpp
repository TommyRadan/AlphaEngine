// Unit tests for core::math mat3 / mat4 and the scalar lerp: matrix products,
// matrix-vector application, the affine builders (translate/rotate/scale), and
// the inverse/transpose identities.

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/mat3.hpp>
#include <core/math/mat4.hpp>
#include <core/math/math.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>

using namespace core::math;

namespace
{
    constexpr float k_eps = 1e-4f;

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    void expect_mat4_near(const mat4& actual, const mat4& expected, float eps = k_eps)
    {
        for (int i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(actual.m[i], expected.m[i], eps) << "element " << i;
        }
    }
}

// -- scalar lerp ------------------------------------------------------------

TEST(scalar_lerp, endpoints_and_midpoint)
{
    EXPECT_FLOAT_EQ(lerp(2.0f, 10.0f, 0.0f), 2.0f);
    EXPECT_FLOAT_EQ(lerp(2.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(lerp(2.0f, 10.0f, 0.5f), 6.0f);
}

// -- mat3 -------------------------------------------------------------------

TEST(mat3, identity_times_vector_is_identity)
{
    mat3 i(1.0f);
    vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(i * v == v);
}

TEST(mat3, transpose_is_involution)
{
    mat3 m;
    for (int k = 0; k < 9; ++k)
    {
        m.m[k] = static_cast<float>(k + 1);
    }
    EXPECT_TRUE(transpose(transpose(m)) == m);
}

TEST(mat3, identity_inverse_is_identity)
{
    EXPECT_TRUE(inverse(mat3(1.0f)) == mat3(1.0f));
}

// -- mat4 -------------------------------------------------------------------

TEST(mat4, default_is_identity)
{
    mat4 i;
    vec4 v(1.0f, 2.0f, 3.0f, 1.0f);
    EXPECT_TRUE(i * v == v);
}

TEST(mat4, identity_is_multiplicative_unit)
{
    mat4 i(1.0f);
    mat4 m = translate(vec3(1.0f, 2.0f, 3.0f));
    expect_mat4_near(i * m, m);
    expect_mat4_near(m * i, m);
}

TEST(mat4, translate_moves_a_point_but_not_a_direction)
{
    mat4 t = translate(vec3(10.0f, 20.0f, 30.0f));
    // w == 1 -> affected by translation.
    vec4 point = t * vec4(1.0f, 2.0f, 3.0f, 1.0f);
    EXPECT_NEAR(point.x, 11.0f, k_eps);
    EXPECT_NEAR(point.y, 22.0f, k_eps);
    EXPECT_NEAR(point.z, 33.0f, k_eps);
    // w == 0 -> a direction, unaffected by translation.
    vec4 dir = t * vec4(1.0f, 2.0f, 3.0f, 0.0f);
    EXPECT_NEAR(dir.x, 1.0f, k_eps);
    EXPECT_NEAR(dir.y, 2.0f, k_eps);
    EXPECT_NEAR(dir.z, 3.0f, k_eps);
}

TEST(mat4, scale_scales_a_point)
{
    mat4 s = scale(vec3(2.0f, 3.0f, 4.0f));
    vec4 p = s * vec4(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_NEAR(p.x, 2.0f, k_eps);
    EXPECT_NEAR(p.y, 3.0f, k_eps);
    EXPECT_NEAR(p.z, 4.0f, k_eps);
}

TEST(mat4, rotate_90_about_z_maps_x_to_y)
{
    mat4 r = rotate(static_cast<float>(M_PI) * 0.5f, vec3(0.0f, 0.0f, 1.0f));
    vec4 rotated = r * vec4(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(rotated.x, 0.0f, k_eps);
    EXPECT_NEAR(rotated.y, 1.0f, k_eps);
    EXPECT_NEAR(rotated.z, 0.0f, k_eps);
}

TEST(mat4, inverse_of_translation_negates_it)
{
    mat4 t = translate(vec3(5.0f, -7.0f, 9.0f));
    expect_mat4_near(inverse(t), translate(vec3(-5.0f, 7.0f, -9.0f)));
}

TEST(mat4, matrix_times_its_inverse_is_identity)
{
    mat4 m = translate(vec3(1.0f, 2.0f, 3.0f));
    m = rotate(m, 0.7f, normalize(vec3(1.0f, 2.0f, 3.0f)));
    m = scale(m, vec3(2.0f, 0.5f, 1.5f));
    expect_mat4_near(m * inverse(m), mat4(1.0f));
}

TEST(mat4, transpose_is_involution)
{
    mat4 m;
    for (int k = 0; k < 16; ++k)
    {
        m.m[k] = static_cast<float>(k + 1);
    }
    EXPECT_TRUE(transpose(transpose(m)) == m);
}

TEST(mat4, look_at_keeps_eye_at_origin_in_view_space)
{
    mat4 view = look_at(vec3(0.0f, 0.0f, 5.0f), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
    // The eye maps to the origin of view space.
    vec4 eye_in_view = view * vec4(0.0f, 0.0f, 5.0f, 1.0f);
    EXPECT_NEAR(eye_in_view.x, 0.0f, k_eps);
    EXPECT_NEAR(eye_in_view.y, 0.0f, k_eps);
    EXPECT_NEAR(eye_in_view.z, 0.0f, k_eps);
    // The target sits down the -Z view axis at the eye-target distance.
    vec4 target_in_view = view * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(target_in_view.z, -5.0f, k_eps);
}

TEST(mat4, perspective_maps_near_plane_to_minus_one_ndc)
{
    const float near_z = 1.0f;
    const float far_z = 100.0f;
    mat4 p = perspective(static_cast<float>(M_PI) * 0.25f, 1.0f, near_z, far_z);
    // A point on the near plane (view space -near_z) maps to NDC z == -1
    // after the perspective divide (OpenGL-style clip range).
    vec4 clip = p * vec4(0.0f, 0.0f, -near_z, 1.0f);
    ASSERT_GT(std::abs(clip.w), k_eps);
    EXPECT_NEAR(clip.z / clip.w, -1.0f, 1e-3f);
}
