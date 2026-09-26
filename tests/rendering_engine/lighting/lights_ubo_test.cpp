// Unit tests for rendering_engine::pack_lights, the packer behind the std140
// Lights block the scene pass uploads every frame: the C++ mirror's layout
// matches the GLSL declaration byte-for-byte, the output is fully
// overwritten, ambient lights sum into one term, directional / point lights
// land in registration order with their direction normalised and colour
// premultiplied by intensity, and lights past each array's capacity are
// dropped without disturbing the other kind. The lights are plain structs
// (their registry is a vector of back-pointers), so this runs device-free.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <core/math/vec3.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/lighting/point_light.hpp>

using core::math::vec3;
using rendering_engine::ambient_light;
using rendering_engine::directional_light;
using rendering_engine::gpu_directional_light;
using rendering_engine::gpu_lights;
using rendering_engine::gpu_point_light;
using rendering_engine::light;
using rendering_engine::max_directional_lights;
using rendering_engine::max_point_lights;
using rendering_engine::pack_lights;
using rendering_engine::point_light;

namespace
{
    constexpr float k_eps = 1e-5f;

    void expect_xyz_near(const float (&actual)[4], const vec3& expected, float w = 0.0f)
    {
        EXPECT_NEAR(actual[0], expected.x, k_eps);
        EXPECT_NEAR(actual[1], expected.y, k_eps);
        EXPECT_NEAR(actual[2], expected.z, k_eps);
        EXPECT_NEAR(actual[3], w, k_eps);
    }

    bool is_zero(const float (&values)[4])
    {
        return values[0] == 0.0f && values[1] == 0.0f && values[2] == 0.0f && values[3] == 0.0f;
    }
} // namespace

// -- std140 mirror ----------------------------------------------------------

TEST(lights_ubo, mirror_structs_match_the_std140_layout)
{
    // DirectionalLight: two vec4s. PointLight: three. The block: a vec4
    // ambient, an ivec4 of counts, then the two arrays on 16-byte
    // boundaries — no implicit padding anywhere, so the struct can be
    // uploaded as one write.
    EXPECT_EQ(sizeof(gpu_directional_light), 32u);
    EXPECT_EQ(sizeof(gpu_point_light), 48u);
    EXPECT_EQ(offsetof(gpu_lights, ambient), 0u);
    EXPECT_EQ(offsetof(gpu_lights, directional_count), 16u);
    EXPECT_EQ(offsetof(gpu_lights, point_count), 20u);
    EXPECT_EQ(offsetof(gpu_lights, directional), 32u);
    const std::size_t directional_bytes = max_directional_lights * sizeof(gpu_directional_light);
    const std::size_t point_bytes = max_point_lights * sizeof(gpu_point_light);
    EXPECT_EQ(offsetof(gpu_lights, point), 32u + directional_bytes);
    EXPECT_EQ(sizeof(gpu_lights), 32u + directional_bytes + point_bytes);
    EXPECT_EQ(sizeof(gpu_lights) % 16u, 0u);
}

// -- packing ----------------------------------------------------------------

TEST(lights_ubo, packing_no_lights_zeroes_the_whole_block)
{
    gpu_lights out{};
    std::memset(&out, 0xAB, sizeof(out)); // stale contents from a previous frame

    pack_lights({}, out);

    EXPECT_TRUE(is_zero(out.ambient));
    EXPECT_EQ(out.directional_count, 0);
    EXPECT_EQ(out.point_count, 0);
    EXPECT_EQ(out.pad0, 0);
    EXPECT_EQ(out.pad1, 0);
    for (const gpu_directional_light& slot : out.directional)
    {
        EXPECT_TRUE(is_zero(slot.direction));
        EXPECT_TRUE(is_zero(slot.color));
    }
    for (const gpu_point_light& slot : out.point)
    {
        EXPECT_TRUE(is_zero(slot.position));
        EXPECT_TRUE(is_zero(slot.color));
        EXPECT_TRUE(is_zero(slot.attenuation));
    }
}

TEST(lights_ubo, ambient_lights_sum_into_one_premultiplied_term)
{
    ambient_light warm;
    warm.color = vec3{1.0f, 0.5f, 0.0f};
    warm.intensity = 2.0f;
    ambient_light cool;
    cool.color = vec3{0.0f, 0.0f, 1.0f};
    cool.intensity = 0.5f;

    gpu_lights out{};
    pack_lights({&warm, &cool}, out);

    expect_xyz_near(out.ambient, vec3{2.0f, 1.0f, 0.5f});
    EXPECT_EQ(out.directional_count, 0);
    EXPECT_EQ(out.point_count, 0);
}

TEST(lights_ubo, a_directional_light_packs_a_unit_direction_and_premultiplied_colour)
{
    directional_light sun;
    sun.direction = vec3{0.0f, -3.0f, 4.0f}; // length 5: the packer must normalise it
    sun.color = vec3{1.0f, 0.5f, 0.25f};
    sun.intensity = 2.0f;

    gpu_lights out{};
    pack_lights({&sun}, out);

    ASSERT_EQ(out.directional_count, 1);
    expect_xyz_near(out.directional[0].direction, vec3{0.0f, -0.6f, 0.8f});
    expect_xyz_near(out.directional[0].color, vec3{2.0f, 1.0f, 0.5f});
    // The source light is left as it was.
    EXPECT_NEAR(sun.direction.y, -3.0f, k_eps);
}

TEST(lights_ubo, a_point_light_packs_position_colour_and_attenuation)
{
    point_light lamp;
    lamp.position = vec3{1.0f, 2.0f, 3.0f};
    lamp.color = vec3{0.5f, 1.0f, 0.0f};
    lamp.intensity = 3.0f;
    lamp.range = 25.0f;
    lamp.constant_attenuation = 1.0f;
    lamp.linear_attenuation = 0.09f;
    lamp.quadratic_attenuation = 0.032f;

    gpu_lights out{};
    pack_lights({&lamp}, out);

    ASSERT_EQ(out.point_count, 1);
    expect_xyz_near(out.point[0].position, vec3{1.0f, 2.0f, 3.0f});
    expect_xyz_near(out.point[0].color, vec3{1.5f, 3.0f, 0.0f});
    EXPECT_NEAR(out.point[0].attenuation[0], 25.0f, k_eps);
    EXPECT_NEAR(out.point[0].attenuation[1], 1.0f, k_eps);
    EXPECT_NEAR(out.point[0].attenuation[2], 0.09f, k_eps);
    EXPECT_NEAR(out.point[0].attenuation[3], 0.032f, k_eps);
}

TEST(lights_ubo, each_kind_fills_its_own_array_in_input_order)
{
    // Interleaved kinds: each array is filled in the order its lights
    // appear, independent of the others, which is what shadow_light_index
    // relies on to name a caster by its slot.
    directional_light first_sun;
    first_sun.direction = vec3{1.0f, 0.0f, 0.0f};
    point_light first_lamp;
    first_lamp.position = vec3{1.0f, 0.0f, 0.0f};
    ambient_light fill;
    fill.intensity = 0.25f;
    directional_light second_sun;
    second_sun.direction = vec3{0.0f, 0.0f, -1.0f};
    point_light second_lamp;
    second_lamp.position = vec3{2.0f, 0.0f, 0.0f};

    gpu_lights out{};
    pack_lights({&first_sun, &first_lamp, &fill, &second_sun, &second_lamp}, out);

    ASSERT_EQ(out.directional_count, 2);
    ASSERT_EQ(out.point_count, 2);
    expect_xyz_near(out.directional[0].direction, vec3{1.0f, 0.0f, 0.0f});
    expect_xyz_near(out.directional[1].direction, vec3{0.0f, 0.0f, -1.0f});
    expect_xyz_near(out.point[0].position, vec3{1.0f, 0.0f, 0.0f});
    expect_xyz_near(out.point[1].position, vec3{2.0f, 0.0f, 0.0f});
    expect_xyz_near(out.ambient, vec3{0.25f, 0.25f, 0.25f});
}

TEST(lights_ubo, directional_lights_past_capacity_are_dropped_without_blocking_point_lights)
{
    std::vector<std::unique_ptr<directional_light>> suns;
    std::vector<light*> lights;
    for (uint32_t i = 0; i < max_directional_lights + 2; ++i)
    {
        auto sun = std::make_unique<directional_light>();
        sun->direction = vec3{0.0f, 0.0f, -1.0f};
        sun->intensity = static_cast<float>(i + 1); // identifies the slot it lands in
        lights.push_back(sun.get());
        suns.push_back(std::move(sun));
    }
    point_light lamp;
    lamp.position = vec3{7.0f, 0.0f, 0.0f};
    lights.push_back(&lamp);

    gpu_lights out{};
    pack_lights(lights, out);

    // The first max_directional_lights are kept in order; the overflow never
    // reaches the block, and the loop carries on to the point light behind it.
    ASSERT_EQ(out.directional_count, static_cast<int32_t>(max_directional_lights));
    for (uint32_t i = 0; i < max_directional_lights; ++i)
    {
        EXPECT_NEAR(out.directional[i].color[0], static_cast<float>(i + 1), k_eps) << "slot " << i;
    }
    ASSERT_EQ(out.point_count, 1);
    expect_xyz_near(out.point[0].position, vec3{7.0f, 0.0f, 0.0f});
}

TEST(lights_ubo, point_lights_past_capacity_are_dropped_without_blocking_directional_lights)
{
    std::vector<std::unique_ptr<point_light>> lamps;
    std::vector<light*> lights;
    for (uint32_t i = 0; i < max_point_lights + 3; ++i)
    {
        auto lamp = std::make_unique<point_light>();
        lamp->position = vec3{static_cast<float>(i), 0.0f, 0.0f};
        lights.push_back(lamp.get());
        lamps.push_back(std::move(lamp));
    }
    directional_light sun;
    sun.direction = vec3{0.0f, -1.0f, 0.0f};
    lights.push_back(&sun);

    gpu_lights out{};
    pack_lights(lights, out);

    ASSERT_EQ(out.point_count, static_cast<int32_t>(max_point_lights));
    for (uint32_t i = 0; i < max_point_lights; ++i)
    {
        EXPECT_NEAR(out.point[i].position[0], static_cast<float>(i), k_eps) << "slot " << i;
    }
    ASSERT_EQ(out.directional_count, 1);
    expect_xyz_near(out.directional[0].direction, vec3{0.0f, -1.0f, 0.0f});
}

TEST(lights_ubo, repacking_a_smaller_set_clears_the_slots_it_no_longer_uses)
{
    directional_light sun;
    sun.direction = vec3{0.0f, 0.0f, -1.0f};
    point_light lamp;
    lamp.position = vec3{3.0f, 3.0f, 3.0f};

    gpu_lights out{};
    pack_lights({&sun, &lamp}, out);
    ASSERT_EQ(out.directional_count, 1);
    ASSERT_EQ(out.point_count, 1);

    // A light switched off between frames must not linger in the block.
    pack_lights({&sun}, out);
    EXPECT_EQ(out.directional_count, 1);
    EXPECT_EQ(out.point_count, 0);
    EXPECT_TRUE(is_zero(out.point[0].position));
    EXPECT_TRUE(is_zero(out.point[0].color));
    EXPECT_TRUE(is_zero(out.point[0].attenuation));
}
