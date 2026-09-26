// Unit tests for the PerDraw block helpers (rendering_engine/renderables/
// per_draw_ubo.hpp): the normal matrix is the inverse-transpose of the
// model's 3x3, a mirroring transform is detected by its determinant so the
// pass can pick the clockwise-front-face variant, and the device helpers
// allocate a block-sized UBO and bind it at the per-draw binding.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

#include "support/fake_device.hpp"

using core::math::mat4;
using core::math::vec3;
using rendering_engine::is_mirrored;
using rendering_engine::make_per_draw_payload;
using rendering_engine::model_determinant;
using rendering_engine::per_draw_payload;
using rendering_engine::per_draw_ubo_size;

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    float at(const mat4& m, int row, int column)
    {
        return m.m[column * 4 + row];
    }

    void expect_upper_3x3_near(const mat4& actual, const mat4& expected)
    {
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 3; ++column)
            {
                EXPECT_NEAR(at(actual, row, column), at(expected, row, column), 1e-5f) << row << "," << column;
            }
        }
    }
} // namespace

TEST(per_draw_ubo, block_is_two_std140_mat4s)
{
    EXPECT_EQ(per_draw_ubo_size, 128u);
    EXPECT_EQ(sizeof(per_draw_payload), 128u);
    EXPECT_EQ(offsetof(per_draw_payload, normal), 64u);
}

TEST(per_draw_ubo, identity_model_has_identity_normal_and_is_not_mirrored)
{
    const per_draw_payload payload = make_per_draw_payload(mat4{});
    EXPECT_EQ(payload.model, mat4{});
    EXPECT_EQ(payload.normal, mat4{});
    EXPECT_FALSE(is_mirrored(mat4{}));
    EXPECT_FLOAT_EQ(model_determinant(mat4{}), 1.0f);
}

TEST(per_draw_ubo, normal_matrix_is_the_inverse_transpose_of_the_upper_3x3)
{
    // Non-uniform scale: the inverse-transpose has the reciprocals on
    // the diagonal, which is what keeps a normal perpendicular to a
    // stretched surface.
    const mat4 model = core::math::scale(vec3{2.0f, 4.0f, 0.5f});
    const per_draw_payload payload = make_per_draw_payload(model);
    EXPECT_EQ(payload.model, model);
    expect_upper_3x3_near(payload.normal, core::math::scale(vec3{0.5f, 0.25f, 2.0f}));
    // The widened rows / columns stay identity so a mat3() read is clean.
    EXPECT_FLOAT_EQ(at(payload.normal, 3, 3), 1.0f);
    EXPECT_FLOAT_EQ(at(payload.normal, 0, 3), 0.0f);
    EXPECT_FLOAT_EQ(at(payload.normal, 3, 0), 0.0f);
}

TEST(per_draw_ubo, rotation_is_its_own_normal_matrix_and_translation_is_ignored)
{
    const mat4 rotation = core::math::rotate(0.3f * pi, vec3{0.0f, 0.0f, 1.0f});
    const mat4 model = core::math::translate(vec3{5.0f, -2.0f, 9.0f}) * rotation;
    const per_draw_payload payload = make_per_draw_payload(model);
    expect_upper_3x3_near(payload.normal, rotation);
    EXPECT_FALSE(is_mirrored(model));
}

TEST(per_draw_ubo, negative_scale_on_one_axis_mirrors)
{
    EXPECT_TRUE(is_mirrored(core::math::scale(vec3{-1.0f, 1.0f, 1.0f})));
    EXPECT_TRUE(is_mirrored(core::math::scale(vec3{1.0f, -2.0f, 1.0f})));
    EXPECT_LT(model_determinant(core::math::scale(vec3{1.0f, 1.0f, -1.0f})), 0.0f);
    // Two negative axes are a rotation, not a mirror.
    EXPECT_FALSE(is_mirrored(core::math::scale(vec3{-1.0f, -1.0f, 1.0f})));
    // All three negative: mirrored again.
    EXPECT_TRUE(is_mirrored(core::math::scale(vec3{-1.0f, -1.0f, -1.0f})));
    // A mirror composed with a rotation and translation still reads as one.
    const mat4 model = core::math::translate(vec3{1.0f, 2.0f, 3.0f}) *
                       core::math::rotate(0.7f, vec3{1.0f, 1.0f, 0.0f}) * core::math::scale(vec3{2.0f, -3.0f, 1.0f});
    EXPECT_TRUE(is_mirrored(model));
}

TEST(per_draw_ubo, singular_model_falls_back_without_nans)
{
    const mat4 flat = core::math::scale(vec3{1.0f, 1.0f, 0.0f});
    const per_draw_payload payload = make_per_draw_payload(flat);
    for (const float value : payload.normal.m)
    {
        EXPECT_FALSE(std::isnan(value));
        EXPECT_FALSE(std::isinf(value));
    }
    EXPECT_FALSE(is_mirrored(flat));
}

TEST(per_draw_ubo, device_helpers_allocate_and_bind_the_block)
{
    test_support::fake_device device;
    const rendering_engine::gpu::buffer ubo = rendering_engine::create_per_draw_ubo(device);
    EXPECT_TRUE(ubo.valid());
    EXPECT_EQ(device.created_buffers, 1u);

    const rendering_engine::gpu::bind_group group =
        rendering_engine::create_per_draw_bind_group(device, rendering_engine::gpu::bind_group_layout{5}, ubo);
    EXPECT_TRUE(group.valid());
    EXPECT_EQ(device.created_bind_groups, 1u);

    EXPECT_FALSE(rendering_engine::write_per_draw_ubo(device, ubo, mat4{}));
    EXPECT_TRUE(rendering_engine::write_per_draw_ubo(device, ubo, core::math::scale(vec3{-1.0f, 1.0f, 1.0f})));
}
