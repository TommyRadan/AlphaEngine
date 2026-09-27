// Unit tests for rendering_engine::camera and the camera registry, headless:
// the projection setters invalidate the cached projection; the view matrix is
// derived from the transform (a direct set_position is never stale, a
// parented transform contributes its world pose, a zero-scale transform stays
// finite); look_at builds a +Z-up basis, straight down included; and the
// registry arbitration — priority, the main tag, most-recent-attach ties,
// disabled cameras skipped, promotion when the winner is destroyed — plus the
// drawable aspect reaching attached and later-attached cameras.

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <core/math/transform.hpp>

using core::math::mat4;
using core::math::vec3;
using core::math::vec4;
using rendering_engine::active_camera;
using rendering_engine::camera;
using rendering_engine::drawable_aspect;
using rendering_engine::main_camera;
using rendering_engine::orthographic_camera;
using rendering_engine::perspective_camera;
using rendering_engine::registered_cameras;
using rendering_engine::set_drawable_aspect;

namespace
{
    constexpr float k_eps = 1e-4f;
    constexpr float k_half_pi = 3.14159265358979323846f * 0.5f;

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    // World point through the view matrix, as a vec3.
    vec3 to_view(const mat4& view, const vec3& world)
    {
        const vec4 v = view * vec4{world, 1.0f};
        return vec3{v.x, v.y, v.z};
    }

    bool is_finite(const mat4& m)
    {
        for (float f : m.m)
        {
            if (!std::isfinite(f))
            {
                return false;
            }
        }
        return true;
    }

    // Every registry test starts and ends with an empty registry: the
    // cameras live on the stack and detach in their destructors.
    struct clean_registry : ::testing::Test
    {
        void SetUp() override
        {
            ASSERT_TRUE(registered_cameras().empty());
            set_drawable_aspect(0.0f);
        }
        void TearDown() override
        {
            set_drawable_aspect(0.0f);
            EXPECT_TRUE(registered_cameras().empty());
        }
    };
} // namespace

// -- projection dirty flag --------------------------------------------------

TEST(perspective_camera, setters_invalidate_the_cached_projection)
{
    perspective_camera cam(1.0f, 1.5f, 0.1f, 100.0f);
    const mat4 initial = cam.get_projection_matrix();
    EXPECT_EQ(cam.get_projection_matrix(), initial);

    cam.set_field_of_view(1.2f);
    const mat4 after_fov = cam.get_projection_matrix();
    EXPECT_NE(after_fov, initial);
    EXPECT_EQ(after_fov, core::math::perspective(1.2f, 1.5f, 0.1f, 100.0f));

    cam.set_aspect_ratio(2.0f);
    const mat4 after_aspect = cam.get_projection_matrix();
    EXPECT_NE(after_aspect, after_fov);
    EXPECT_EQ(after_aspect, core::math::perspective(1.2f, 2.0f, 0.1f, 100.0f));

    cam.set_near_clip(0.5f);
    const mat4 after_near = cam.get_projection_matrix();
    EXPECT_NE(after_near, after_aspect);

    cam.set_far_clip(50.0f);
    const mat4 after_far = cam.get_projection_matrix();
    EXPECT_NE(after_far, after_near);
    EXPECT_EQ(after_far, core::math::perspective(1.2f, 2.0f, 0.5f, 50.0f));

    EXPECT_FLOAT_EQ(cam.get_field_of_view(), 1.2f);
    EXPECT_FLOAT_EQ(cam.get_aspect_ratio(), 2.0f);
    EXPECT_FLOAT_EQ(cam.get_near_clip(), 0.5f);
    EXPECT_FLOAT_EQ(cam.get_far_clip(), 50.0f);
}

TEST(orthographic_camera, setters_invalidate_the_cached_projection)
{
    orthographic_camera cam;
    const mat4 initial = cam.get_projection_matrix();
    EXPECT_EQ(initial, core::math::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10000.0f));

    cam.set_x_magnification(4.0f);
    EXPECT_NE(cam.get_projection_matrix(), initial);
    cam.set_y_magnification(3.0f);
    cam.set_near_clip(1.0f);
    cam.set_far_clip(20.0f);
    EXPECT_EQ(cam.get_projection_matrix(), core::math::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 20.0f));
    EXPECT_FLOAT_EQ(cam.get_x_magnification(), 4.0f);
    EXPECT_FLOAT_EQ(cam.get_y_magnification(), 3.0f);
    // The drawable aspect is meaningless for an orthographic box: ignored.
    cam.set_aspect_ratio(16.0f / 9.0f);
    EXPECT_EQ(cam.get_projection_matrix(), core::math::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 1.0f, 20.0f));
}

// -- view matrix from the transform -----------------------------------------

TEST(camera, identity_orientation_looks_along_plus_x_with_plus_z_up)
{
    orthographic_camera cam;
    cam.transform.set_position(vec3{1.0f, 2.0f, 3.0f});
    const mat4 view = cam.get_view_matrix();
    // The eye maps to the view-space origin, forward to -Z, up to +Y and
    // the right side (-Y in the world) to +X.
    expect_vec3_near(to_view(view, vec3{1.0f, 2.0f, 3.0f}), vec3{0.0f, 0.0f, 0.0f});
    expect_vec3_near(to_view(view, vec3{2.0f, 2.0f, 3.0f}), vec3{0.0f, 0.0f, -1.0f});
    expect_vec3_near(to_view(view, vec3{1.0f, 2.0f, 4.0f}), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(to_view(view, vec3{1.0f, 1.0f, 3.0f}), vec3{1.0f, 0.0f, 0.0f});
}

TEST(camera, a_direct_transform_change_is_reflected_in_the_next_view_matrix)
{
    orthographic_camera cam;
    cam.transform.set_position(vec3{0.0f, 0.0f, 0.0f});
    const mat4 first = cam.get_view_matrix();
    expect_vec3_near(to_view(first, vec3{5.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -5.0f});

    // No invalidate call: the view is derived from the transform each time.
    cam.transform.set_position(vec3{4.0f, 0.0f, 0.0f});
    const mat4 second = cam.get_view_matrix();
    EXPECT_NE(second, first);
    expect_vec3_near(to_view(second, vec3{5.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});

    cam.transform.set_rotation(vec3{0.0f, 0.0f, k_half_pi});
    expect_vec3_near(to_view(cam.get_view_matrix(), vec3{4.0f, 1.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});
}

TEST(camera, look_at_builds_a_plus_z_up_basis)
{
    orthographic_camera cam;
    cam.transform.set_position(vec3{0.0f, -5.0f, 0.0f});
    cam.look_at(vec3{0.0f, 0.0f, 0.0f});
    expect_vec3_near(cam.transform.get_forward(), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(cam.transform.get_up(), core::math::world_up);
    expect_vec3_near(cam.transform.get_right(), vec3{1.0f, 0.0f, 0.0f});

    const mat4 view = cam.get_view_matrix();
    expect_vec3_near(to_view(view, vec3{0.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -5.0f});
    expect_vec3_near(to_view(view, vec3{0.0f, -5.0f, 1.0f}), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(to_view(view, vec3{1.0f, -5.0f, 0.0f}), vec3{1.0f, 0.0f, 0.0f});
    // The basis is what the world matrix carries, so a camera in the node
    // hierarchy composes correctly.
    const mat4 world = cam.transform.get_world_matrix();
    expect_vec3_near(vec3{world.m[0], world.m[1], world.m[2]}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(vec3{world.m[8], world.m[9], world.m[10]}, core::math::world_up);
}

TEST(camera, look_at_straight_down_keeps_a_finite_view_with_a_horizontal_up)
{
    orthographic_camera cam;
    cam.transform.set_position(vec3{0.0f, 0.0f, 10.0f});
    cam.look_at(vec3{0.0f, 0.0f, 0.0f});
    expect_vec3_near(cam.transform.get_forward(), vec3{0.0f, 0.0f, -1.0f});
    // The fallback reference up is the engine forward, +X.
    expect_vec3_near(cam.transform.get_up(), vec3{1.0f, 0.0f, 0.0f});

    const mat4 view = cam.get_view_matrix();
    EXPECT_TRUE(is_finite(view));
    expect_vec3_near(to_view(view, vec3{0.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -10.0f});
    expect_vec3_near(to_view(view, vec3{1.0f, 0.0f, 10.0f}), vec3{0.0f, 1.0f, 0.0f});

    cam.look_at(vec3{0.0f, 0.0f, 20.0f});
    expect_vec3_near(cam.transform.get_forward(), vec3{0.0f, 0.0f, 1.0f});
    EXPECT_TRUE(is_finite(cam.get_view_matrix()));
}

TEST(camera, a_parented_transform_views_from_its_world_pose)
{
    core::transform rig;
    rig.set_position(vec3{10.0f, 0.0f, 0.0f});
    rig.set_rotation(vec3{0.0f, 0.0f, k_half_pi});

    orthographic_camera cam;
    cam.transform.set_parent(&rig);
    // Local identity: the world pose is the rig's, so the camera sits at
    // (10, 0, 0) facing +Y.
    const mat4 view = cam.get_view_matrix();
    expect_vec3_near(to_view(view, vec3{10.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, 0.0f});
    expect_vec3_near(to_view(view, vec3{10.0f, 1.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});
    expect_vec3_near(to_view(view, vec3{10.0f, 0.0f, 1.0f}), vec3{0.0f, 1.0f, 0.0f});

    // Moving the rig moves the camera with no call on the camera.
    rig.set_position(vec3{20.0f, 0.0f, 0.0f});
    expect_vec3_near(to_view(cam.get_view_matrix(), vec3{20.0f, 3.0f, 0.0f}), vec3{0.0f, 0.0f, -3.0f});
    cam.transform.set_parent(nullptr);
}

TEST(camera, look_at_under_a_rotated_parent_faces_the_world_target)
{
    core::transform rig;
    rig.set_rotation(vec3{0.0f, 0.0f, k_half_pi});

    orthographic_camera cam;
    cam.transform.set_parent(&rig);
    cam.look_at(vec3{5.0f, 0.0f, 0.0f});
    const mat4 view = cam.get_view_matrix();
    expect_vec3_near(to_view(view, vec3{5.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -5.0f});
    expect_vec3_near(to_view(view, vec3{0.0f, 0.0f, 1.0f}), vec3{0.0f, 1.0f, 0.0f});
    cam.transform.set_parent(nullptr);
}

TEST(camera, a_zero_scale_transform_yields_a_finite_identity_oriented_view)
{
    orthographic_camera cam;
    cam.transform.set_position(vec3{1.0f, 0.0f, 0.0f});
    cam.transform.set_scale(vec3{0.0f, 0.0f, 0.0f});
    const mat4 view = cam.get_view_matrix();
    EXPECT_TRUE(is_finite(view));
    expect_vec3_near(to_view(view, vec3{2.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});
    const core::math::vec4 near_plane = cam.get_frustum().planes[0];
    EXPECT_TRUE(std::isfinite(near_plane.x) && std::isfinite(near_plane.y) && std::isfinite(near_plane.z) &&
                std::isfinite(near_plane.w));
}

TEST(camera, a_scaled_transform_does_not_scale_the_view)
{
    orthographic_camera cam;
    cam.transform.set_scale(vec3{3.0f, 3.0f, 3.0f});
    expect_vec3_near(to_view(cam.get_view_matrix(), vec3{1.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});
}

// -- registry arbitration ---------------------------------------------------

TEST_F(clean_registry, no_attached_camera_means_no_active_camera)
{
    orthographic_camera cam;
    EXPECT_EQ(active_camera(), nullptr);
    EXPECT_FALSE(cam.is_attached());
    cam.attach();
    EXPECT_TRUE(cam.is_attached());
    EXPECT_EQ(active_camera(), &cam);
    cam.detach();
    EXPECT_FALSE(cam.is_attached());
    EXPECT_EQ(active_camera(), nullptr);
    // Detaching twice is harmless.
    cam.detach();
}

TEST_F(clean_registry, the_highest_priority_enabled_camera_is_active)
{
    orthographic_camera low;
    orthographic_camera high;
    high.set_priority(10);
    high.attach();
    low.attach();
    EXPECT_EQ(active_camera(), &high);

    low.set_priority(20);
    EXPECT_EQ(active_camera(), &low);
}

TEST_F(clean_registry, disabled_cameras_are_skipped_and_re_enabled_ones_return)
{
    orthographic_camera a;
    orthographic_camera b;
    b.set_priority(5);
    a.attach();
    b.attach();
    ASSERT_EQ(active_camera(), &b);

    b.set_enabled(false);
    EXPECT_TRUE(b.is_attached());
    EXPECT_EQ(active_camera(), &a);

    a.set_enabled(false);
    EXPECT_EQ(active_camera(), nullptr);

    b.set_enabled(true);
    EXPECT_EQ(active_camera(), &b);
}

TEST_F(clean_registry, the_most_recently_attached_wins_a_priority_tie)
{
    orthographic_camera first;
    orthographic_camera second;
    first.attach();
    second.attach();
    EXPECT_EQ(active_camera(), &second);

    // Re-attaching moves a camera to the back, so it takes the tie again.
    first.attach();
    EXPECT_EQ(active_camera(), &first);
    EXPECT_EQ(registered_cameras().size(), 2u);
}

TEST_F(clean_registry, a_main_camera_wins_a_priority_tie_but_not_a_higher_priority)
{
    orthographic_camera main;
    orthographic_camera later;
    main.set_main(true);
    main.attach();
    later.attach();
    EXPECT_EQ(active_camera(), &main);
    EXPECT_EQ(main_camera(), &main);

    later.set_priority(1);
    EXPECT_EQ(active_camera(), &later);
    // main_camera still finds the player's camera behind the override.
    EXPECT_EQ(main_camera(), &main);

    main.set_enabled(false);
    EXPECT_EQ(main_camera(), nullptr);
}

TEST_F(clean_registry, destroying_the_active_camera_promotes_the_next)
{
    orthographic_camera fallback;
    fallback.attach();
    {
        orthographic_camera winner;
        winner.set_priority(3);
        winner.attach();
        ASSERT_EQ(active_camera(), &winner);
    }
    EXPECT_EQ(active_camera(), &fallback);
    EXPECT_EQ(registered_cameras().size(), 1u);
}

TEST_F(clean_registry, the_drawable_aspect_reaches_attached_and_later_attached_cameras)
{
    perspective_camera attached(1.0f, 1.0f);
    perspective_camera unattached(1.0f, 1.0f);
    attached.attach();

    set_drawable_aspect(2.0f);
    EXPECT_FLOAT_EQ(drawable_aspect(), 2.0f);
    EXPECT_FLOAT_EQ(attached.get_aspect_ratio(), 2.0f);
    EXPECT_FLOAT_EQ(unattached.get_aspect_ratio(), 1.0f);

    // Attaching later picks the last reported aspect up.
    unattached.attach();
    EXPECT_FLOAT_EQ(unattached.get_aspect_ratio(), 2.0f);

    // Clearing it stops the hand-out without touching the cameras.
    set_drawable_aspect(0.0f);
    EXPECT_FLOAT_EQ(drawable_aspect(), 0.0f);
    perspective_camera late(1.0f, 1.0f);
    late.attach();
    EXPECT_FLOAT_EQ(late.get_aspect_ratio(), 1.0f);
}
