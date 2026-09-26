// Unit tests for core::math mat3 / mat4 and the scalar lerp: matrix products,
// matrix-vector application (both mat * vec and the row-vector vec * mat),
// the affine builders (translate/rotate/scale), the inverse/transpose
// identities, and the look_at / perspective / ortho projections.

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
    constexpr float k_pi = 3.14159265358979323846f;

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
    mat4 r = rotate(k_pi * 0.5f, vec3(0.0f, 0.0f, 1.0f));
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
    mat4 p = perspective(k_pi * 0.25f, 1.0f, near_z, far_z);
    // A point on the near plane (view space -near_z) maps to NDC z == -1
    // after the perspective divide (OpenGL-style clip range).
    vec4 clip = p * vec4(0.0f, 0.0f, -near_z, 1.0f);
    ASSERT_GT(std::abs(clip.w), k_eps);
    EXPECT_NEAR(clip.z / clip.w, -1.0f, 1e-3f);
}

TEST(mat4, ortho_maps_the_box_onto_the_ndc_cube)
{
    // An off-centre, non-square box so every axis has a distinct scale and
    // offset. GL clip conventions: x/y land in [-1, 1] left-to-right and
    // bottom-to-top, the near plane (view-space z = -near_z) on NDC z = -1
    // and the far plane on +1, with w untouched (no perspective divide).
    const float left = -2.0f;
    const float right = 6.0f;
    const float bottom = 1.0f;
    const float top = 5.0f;
    const float near_z = 0.5f;
    const float far_z = 20.5f;
    mat4 o = ortho(left, right, bottom, top, near_z, far_z);

    vec4 lower_near = o * vec4(left, bottom, -near_z, 1.0f);
    expect_vec3_near(vec3(lower_near.x, lower_near.y, lower_near.z), vec3(-1.0f, -1.0f, -1.0f));
    EXPECT_NEAR(lower_near.w, 1.0f, k_eps);

    vec4 upper_far = o * vec4(right, top, -far_z, 1.0f);
    expect_vec3_near(vec3(upper_far.x, upper_far.y, upper_far.z), vec3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(upper_far.w, 1.0f, k_eps);

    // The box centre maps to the NDC origin, and it is affine: a step of a
    // quarter of the width in x moves half a unit in NDC.
    vec4 centre = o * vec4((left + right) * 0.5f, (bottom + top) * 0.5f, -(near_z + far_z) * 0.5f, 1.0f);
    expect_vec3_near(vec3(centre.x, centre.y, centre.z), vec3(0.0f, 0.0f, 0.0f));
    vec4 shifted = o * vec4((left + right) * 0.5f + (right - left) * 0.25f, bottom, -near_z, 1.0f);
    EXPECT_NEAR(shifted.x, 0.5f, k_eps);
}

TEST(mat4, ortho_is_a_pure_scale_and_translate)
{
    // Directions (w == 0) are only scaled, never offset, and the scale is
    // 2 / extent on each axis (negated on z, since the view looks down -Z).
    mat4 o = ortho(-4.0f, 4.0f, -2.0f, 2.0f, 1.0f, 11.0f);
    vec4 d = o * vec4(4.0f, 2.0f, -5.0f, 0.0f);
    EXPECT_NEAR(d.x, 1.0f, k_eps);
    EXPECT_NEAR(d.y, 1.0f, k_eps);
    EXPECT_NEAR(d.z, 1.0f, k_eps);
    EXPECT_NEAR(d.w, 0.0f, k_eps);

    // Invertible, as a projection the shadow pass unprojects through must be.
    expect_mat4_near(o * inverse(o), mat4(1.0f));
}

// -- vec4 * mat4 (row-vector product) -------------------------------------------

TEST(mat4, vector_times_matrix_is_the_transposed_product)
{
    // v * M treats v as a row vector: it equals transpose(M) * v, so the two
    // overloads agree only for a symmetric matrix. camera_module uses this
    // form to pull a world-space axis through a view matrix.
    mat4 m;
    for (int k = 0; k < 16; ++k)
    {
        m.m[k] = static_cast<float>(k * k % 7 + 1);
    }
    const vec4 v(1.0f, -2.0f, 3.0f, 0.5f);

    const vec4 row = v * m;
    const vec4 via_transpose = transpose(m) * v;
    EXPECT_NEAR(row.x, via_transpose.x, k_eps);
    EXPECT_NEAR(row.y, via_transpose.y, k_eps);
    EXPECT_NEAR(row.z, via_transpose.z, k_eps);
    EXPECT_NEAR(row.w, via_transpose.w, k_eps);

    // Written out: component i of v * M is the dot of v with column i,
    // i.e. with m.m[i * 4 .. i * 4 + 3] in column-major storage.
    for (int col = 0; col < 4; ++col)
    {
        const float expected = v.x * m.m[col * 4 + 0] + v.y * m.m[col * 4 + 1] + v.z * m.m[col * 4 + 2] +
                               v.w * m.m[col * 4 + 3];
        EXPECT_NEAR(row.data()[col], expected, k_eps) << "component " << col;
    }
}

TEST(mat4, vector_times_matrix_differs_from_matrix_times_vector)
{
    // A translation is the simplest asymmetric case: as a row vector the
    // point is not moved (the translation sits in the last column, which
    // v * M reads into w), whereas M * v moves it.
    mat4 t = translate(vec3(10.0f, 20.0f, 30.0f));
    const vec4 p(1.0f, 2.0f, 3.0f, 1.0f);

    const vec4 column_form = t * p;
    expect_vec3_near(vec3(column_form.x, column_form.y, column_form.z), vec3(11.0f, 22.0f, 33.0f));

    const vec4 row_form = p * t;
    expect_vec3_near(vec3(row_form.x, row_form.y, row_form.z), vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(row_form.w, 10.0f + 40.0f + 90.0f + 1.0f, k_eps);

    // Identity is the one case where the two agree for every vector.
    const vec4 through_identity = p * mat4(1.0f);
    EXPECT_TRUE(through_identity == p);
}

TEST(mat4, vector_times_matrix_undoes_the_rotation_of_matrix_times_vector)
{
    // For a rotation R, transpose(R) == inverse(R): pulling a vector through
    // R as a row vector is the inverse rotation of pushing it through as a
    // column — how a view-space axis is taken back to world space.
    mat4 r = rotate(k_pi * 0.5f, vec3(0.0f, 0.0f, 1.0f));
    const vec4 x(1.0f, 0.0f, 0.0f, 0.0f);

    const vec4 rotated = r * x;
    expect_vec3_near(vec3(rotated.x, rotated.y, rotated.z), vec3(0.0f, 1.0f, 0.0f));

    const vec4 back = rotated * r;
    expect_vec3_near(vec3(back.x, back.y, back.z), vec3(1.0f, 0.0f, 0.0f));
}
