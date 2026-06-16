// Unit tests for core::math bounding volumes: aabb, sphere, and frustum
// culling against boxes and spheres.

#include <gtest/gtest.h>

#include <cmath>

#include <core/math/aabb.hpp>
#include <core/math/frustum.hpp>
#include <core/math/mat4.hpp>
#include <core/math/sphere.hpp>
#include <core/math/vec3.hpp>

using namespace core::math;

namespace
{
    constexpr float k_eps = 1e-5f;

    // A view-projection for a camera at the origin looking down -Z.
    mat4 make_view_projection()
    {
        mat4 view = look_at(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
        mat4 proj = perspective(static_cast<float>(M_PI) * 0.5f, 1.0f, 0.1f, 100.0f);
        return proj * view;
    }
}

// -- aabb -------------------------------------------------------------------

TEST(aabb, center_and_extents)
{
    aabb box(vec3(-2.0f, -4.0f, -6.0f), vec3(2.0f, 4.0f, 6.0f));
    vec3 c = box.center();
    EXPECT_NEAR(c.x, 0.0f, k_eps);
    EXPECT_NEAR(c.y, 0.0f, k_eps);
    EXPECT_NEAR(c.z, 0.0f, k_eps);
    vec3 e = box.extents();
    EXPECT_NEAR(e.x, 2.0f, k_eps);
    EXPECT_NEAR(e.y, 4.0f, k_eps);
    EXPECT_NEAR(e.z, 6.0f, k_eps);
}

TEST(aabb, contains_points_inside_and_outside)
{
    aabb box(vec3(0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(box.contains(vec3(0.5f, 0.5f, 0.5f)));
    EXPECT_TRUE(box.contains(vec3(0.0f, 0.0f, 0.0f))); // on the boundary
    EXPECT_TRUE(box.contains(vec3(1.0f, 1.0f, 1.0f))); // on the boundary
    EXPECT_FALSE(box.contains(vec3(1.5f, 0.5f, 0.5f)));
    EXPECT_FALSE(box.contains(vec3(-0.1f, 0.5f, 0.5f)));
}

TEST(aabb, merge_boxes_covers_both)
{
    aabb a(vec3(0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));
    aabb b(vec3(-1.0f, 2.0f, 0.5f), vec3(0.5f, 3.0f, 4.0f));
    aabb m = merge(a, b);
    EXPECT_TRUE(m.contains(a.center()));
    EXPECT_TRUE(m.contains(b.center()));
    EXPECT_NEAR(m.min.x, -1.0f, k_eps);
    EXPECT_NEAR(m.min.y, 0.0f, k_eps);
    EXPECT_NEAR(m.max.z, 4.0f, k_eps);
}

TEST(aabb, merge_with_point_expands_to_include_it)
{
    aabb a(vec3(0.0f, 0.0f, 0.0f), vec3(1.0f, 1.0f, 1.0f));
    aabb m = merge(a, vec3(5.0f, -2.0f, 0.5f));
    EXPECT_TRUE(m.contains(vec3(5.0f, -2.0f, 0.5f)));
    EXPECT_NEAR(m.max.x, 5.0f, k_eps);
    EXPECT_NEAR(m.min.y, -2.0f, k_eps);
}

// -- sphere -----------------------------------------------------------------

TEST(sphere, contains_points_inside_and_outside)
{
    sphere s(vec3(0.0f, 0.0f, 0.0f), 2.0f);
    EXPECT_TRUE(s.contains(vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(s.contains(vec3(2.0f, 0.0f, 0.0f))); // on the surface
    EXPECT_FALSE(s.contains(vec3(2.01f, 0.0f, 0.0f)));
    EXPECT_FALSE(s.contains(vec3(2.0f, 2.0f, 2.0f)));
}

TEST(sphere, merge_with_point_covers_it)
{
    sphere s(vec3(0.0f, 0.0f, 0.0f), 1.0f);
    sphere m = merge(s, vec3(5.0f, 0.0f, 0.0f));
    EXPECT_TRUE(m.contains(vec3(5.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(m.contains(vec3(-1.0f, 0.0f, 0.0f))); // original extent still covered
}

TEST(sphere, merge_contained_sphere_is_noop_on_coverage)
{
    sphere big(vec3(0.0f, 0.0f, 0.0f), 10.0f);
    sphere small(vec3(1.0f, 0.0f, 0.0f), 1.0f);
    sphere m = merge(big, small);
    EXPECT_GE(m.radius, big.radius - k_eps);
    EXPECT_TRUE(m.contains(vec3(9.0f, 0.0f, 0.0f)));
}

// -- frustum ----------------------------------------------------------------

TEST(frustum, sphere_in_front_intersects_and_behind_does_not)
{
    frustum f = frustum::from_view_projection(make_view_projection());
    EXPECT_TRUE(f.intersects(sphere(vec3(0.0f, 0.0f, -5.0f), 1.0f)));
    // Behind the camera (+Z) — fully outside the near plane.
    EXPECT_FALSE(f.intersects(sphere(vec3(0.0f, 0.0f, 5.0f), 1.0f)));
    // Far beyond the far plane.
    EXPECT_FALSE(f.intersects(sphere(vec3(0.0f, 0.0f, -200.0f), 1.0f)));
}

TEST(frustum, box_in_front_intersects_and_far_aside_does_not)
{
    frustum f = frustum::from_view_projection(make_view_projection());
    EXPECT_TRUE(f.intersects(aabb(vec3(-1.0f, -1.0f, -6.0f), vec3(1.0f, 1.0f, -4.0f))));
    // Far off to the side at the camera plane — outside the lateral planes.
    EXPECT_FALSE(f.intersects(aabb(vec3(1000.0f, -1.0f, -5.0f), vec3(1002.0f, 1.0f, -4.0f))));
}

TEST(frustum, large_box_straddling_camera_intersects)
{
    frustum f = frustum::from_view_projection(make_view_projection());
    // A box big enough to enclose part of the visible volume.
    EXPECT_TRUE(f.intersects(aabb(vec3(-10.0f, -10.0f, -10.0f), vec3(10.0f, 10.0f, 10.0f))));
}
