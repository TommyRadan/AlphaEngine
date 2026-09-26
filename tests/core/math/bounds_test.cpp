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
    constexpr float k_pi = 3.14159265358979323846f;

    // A view-projection for a camera at the origin looking down -Z.
    mat4 make_view_projection()
    {
        mat4 view = look_at(vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, -1.0f), vec3(0.0f, 1.0f, 0.0f));
        mat4 proj = perspective(k_pi * 0.5f, 1.0f, 0.1f, 100.0f);
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

TEST(sphere, merge_returns_the_enclosing_sphere_whichever_side_it_is_on)
{
    sphere big(vec3(0.0f, 0.0f, 0.0f), 10.0f);
    sphere small(vec3(1.0f, 0.0f, 0.0f), 1.0f);

    // Neither grows: the enclosing one comes back unchanged from either side.
    sphere a = merge(big, small);
    sphere b = merge(small, big);
    EXPECT_NEAR(a.radius, 10.0f, k_eps);
    EXPECT_NEAR(b.radius, 10.0f, k_eps);
    EXPECT_NEAR(a.center.x, 0.0f, k_eps);
    EXPECT_NEAR(b.center.x, 0.0f, k_eps);

    // Merging a sphere with itself, or with a copy, is the identity.
    sphere self = merge(big, big);
    EXPECT_NEAR(self.radius, 10.0f, k_eps);
    EXPECT_NEAR(self.center.x, 0.0f, k_eps);
}

TEST(sphere, merge_of_partially_overlapping_spheres_is_the_tight_enclosing_sphere)
{
    // Centres 4 apart along x, radii 2 and 3: the union spans from -2 to 7 on
    // the x axis, so the smallest enclosing sphere is centred at 2.5 with
    // radius 4.5 — (distance + ra + rb) / 2 on the segment between them.
    sphere a(vec3(0.0f, 0.0f, 0.0f), 2.0f);
    sphere b(vec3(4.0f, 0.0f, 0.0f), 3.0f);

    sphere m = merge(a, b);
    EXPECT_NEAR(m.radius, 4.5f, k_eps);
    EXPECT_NEAR(m.center.x, 2.5f, k_eps);
    EXPECT_NEAR(m.center.y, 0.0f, k_eps);
    EXPECT_NEAR(m.center.z, 0.0f, k_eps);

    // Both inputs' far surface points lie on the result.
    EXPECT_TRUE(m.contains(vec3(-2.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(m.contains(vec3(7.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(m.contains(vec3(-2.01f, 0.0f, 0.0f)));
    EXPECT_FALSE(m.contains(vec3(7.01f, 0.0f, 0.0f)));
    // And the perpendicular extremes of both inputs are inside.
    EXPECT_TRUE(m.contains(vec3(0.0f, 2.0f, 0.0f)));
    EXPECT_TRUE(m.contains(vec3(4.0f, 0.0f, 3.0f)));

    // The operation is symmetric.
    sphere reversed = merge(b, a);
    EXPECT_NEAR(reversed.radius, m.radius, k_eps);
    EXPECT_NEAR(reversed.center.x, m.center.x, k_eps);
}

TEST(sphere, merge_of_disjoint_spheres_spans_the_gap_between_them)
{
    // No overlap, off-axis: the result still covers both and is no larger
    // than needed (its diameter is the far-surface-to-far-surface distance).
    sphere a(vec3(-3.0f, 0.0f, 0.0f), 1.0f);
    sphere b(vec3(3.0f, 8.0f, 0.0f), 2.0f);
    const float centre_distance = 10.0f; // (-3,0) to (3,8)

    sphere m = merge(a, b);
    EXPECT_NEAR(m.radius, (centre_distance + 1.0f + 2.0f) * 0.5f, k_eps);

    // The centre sits on the segment between the inputs' centres, offset
    // from a by (new_radius - a.radius) along it.
    const float t = (m.radius - a.radius) / centre_distance;
    EXPECT_NEAR(m.center.x, -3.0f + 6.0f * t, k_eps);
    EXPECT_NEAR(m.center.y, 0.0f + 8.0f * t, k_eps);
    EXPECT_NEAR(m.center.z, 0.0f, k_eps);

    // The far surface point of each input lies on the merged sphere.
    const vec3 axis = normalize(b.center - a.center);
    const vec3 a_far = a.center - axis * a.radius;
    const vec3 b_far = b.center + axis * b.radius;
    EXPECT_NEAR(distance(m.center, a_far), m.radius, 1e-4f);
    EXPECT_NEAR(distance(m.center, b_far), m.radius, 1e-4f);
}

TEST(sphere, merge_with_a_point_sphere_behaves_like_merge_with_the_point)
{
    sphere s(vec3(0.0f, 0.0f, 0.0f), 1.0f);
    const vec3 p(5.0f, 0.0f, 0.0f);

    sphere via_point = merge(s, p);
    sphere via_sphere = merge(s, sphere(p, 0.0f));
    EXPECT_NEAR(via_point.radius, 3.0f, k_eps);
    EXPECT_NEAR(via_point.center.x, 2.0f, k_eps);
    EXPECT_NEAR(via_sphere.radius, via_point.radius, k_eps);
    EXPECT_NEAR(via_sphere.center.x, via_point.center.x, k_eps);

    // A point already inside changes nothing.
    sphere unchanged = merge(s, vec3(0.5f, 0.5f, 0.0f));
    EXPECT_NEAR(unchanged.radius, 1.0f, k_eps);
    EXPECT_NEAR(unchanged.center.x, 0.0f, k_eps);
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

// -- aabb transform -----------------------------------------------------------

TEST(aabb, transform_by_translation_moves_the_box)
{
    aabb box(vec3(-1.0f, -2.0f, -3.0f), vec3(1.0f, 2.0f, 3.0f));
    aabb t = transform(box, translate(vec3(10.0f, 20.0f, 30.0f)));
    EXPECT_NEAR(t.min.x, 9.0f, k_eps);
    EXPECT_NEAR(t.max.x, 11.0f, k_eps);
    EXPECT_NEAR(t.min.y, 18.0f, k_eps);
    EXPECT_NEAR(t.max.y, 22.0f, k_eps);
    EXPECT_NEAR(t.min.z, 27.0f, k_eps);
    EXPECT_NEAR(t.max.z, 33.0f, k_eps);
}

TEST(aabb, transform_by_scale_scales_the_extents)
{
    aabb box(vec3(-1.0f, -1.0f, -1.0f), vec3(1.0f, 1.0f, 1.0f));
    aabb t = transform(box, scale(vec3(2.0f, 3.0f, 0.5f)));
    EXPECT_NEAR(t.min.x, -2.0f, k_eps);
    EXPECT_NEAR(t.max.x, 2.0f, k_eps);
    EXPECT_NEAR(t.min.y, -3.0f, k_eps);
    EXPECT_NEAR(t.max.y, 3.0f, k_eps);
    EXPECT_NEAR(t.min.z, -0.5f, k_eps);
    EXPECT_NEAR(t.max.z, 0.5f, k_eps);
}

TEST(aabb, transform_by_rotation_reboxes_the_rotated_corners)
{
    // A unit cube turned 45 degrees about Z spans sqrt(2) along X and Y;
    // Z is untouched.
    aabb box(vec3(-1.0f, -1.0f, -1.0f), vec3(1.0f, 1.0f, 1.0f));
    aabb t = transform(box, rotate(k_pi * 0.25f, vec3(0.0f, 0.0f, 1.0f)));
    const float s = std::sqrt(2.0f);
    EXPECT_NEAR(t.min.x, -s, 1e-4f);
    EXPECT_NEAR(t.max.x, s, 1e-4f);
    EXPECT_NEAR(t.min.y, -s, 1e-4f);
    EXPECT_NEAR(t.max.y, s, 1e-4f);
    EXPECT_NEAR(t.min.z, -1.0f, 1e-4f);
    EXPECT_NEAR(t.max.z, 1.0f, 1e-4f);
}

TEST(aabb, transform_matches_the_eight_transformed_corners)
{
    // Off-centre box under translation * rotation * non-uniform scale: the
    // closed form must equal the brute-force re-box of the corners.
    aabb box(vec3(-0.5f, 1.0f, -2.0f), vec3(1.5f, 3.0f, 0.5f));
    mat4 m = translate(vec3(3.0f, -1.0f, 2.0f)) * rotate(0.7f, normalize(vec3(1.0f, 2.0f, 3.0f))) *
             scale(vec3(2.0f, 0.5f, 1.5f));
    aabb t = transform(box, m);

    aabb expected;
    for (int i = 0; i < 8; ++i)
    {
        const vec3 corner((i & 1) != 0 ? box.max.x : box.min.x,
                          (i & 2) != 0 ? box.max.y : box.min.y,
                          (i & 4) != 0 ? box.max.z : box.min.z);
        const vec4 p = m * vec4(corner, 1.0f);
        const vec3 q(p.x, p.y, p.z);
        expected = i == 0 ? aabb(q, q) : merge(expected, q);
    }

    EXPECT_NEAR(t.min.x, expected.min.x, 1e-4f);
    EXPECT_NEAR(t.min.y, expected.min.y, 1e-4f);
    EXPECT_NEAR(t.min.z, expected.min.z, 1e-4f);
    EXPECT_NEAR(t.max.x, expected.max.x, 1e-4f);
    EXPECT_NEAR(t.max.y, expected.max.y, 1e-4f);
    EXPECT_NEAR(t.max.z, expected.max.z, 1e-4f);
}

// -- frustum culling ------------------------------------------------------------

TEST(frustum, culls_unit_boxes_around_a_perspective_camera)
{
    // Camera at the origin looking down -Z, 90 degree FOV, square aspect,
    // near 0.1, far 100: the side planes are x = -z and y = -z.
    frustum f = frustum::from_view_projection(make_view_projection());
    const aabb unit(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 0.5f));
    const auto at = [&](float x, float y, float z) { return transform(unit, translate(vec3(x, y, z))); };

    EXPECT_TRUE(f.intersects(at(0.0f, 0.0f, -10.0f)));  // dead ahead
    EXPECT_TRUE(f.intersects(at(8.0f, 0.0f, -10.0f)));  // inside the right edge
    EXPECT_TRUE(f.intersects(at(10.4f, 0.0f, -10.0f))); // straddling the right plane
    EXPECT_TRUE(f.intersects(at(0.0f, 0.0f, -99.8f)));  // straddling the far plane

    EXPECT_FALSE(f.intersects(at(0.0f, 0.0f, 10.0f)));   // behind the camera
    EXPECT_FALSE(f.intersects(at(0.0f, 0.0f, -150.0f))); // beyond the far plane
    EXPECT_FALSE(f.intersects(at(30.0f, 0.0f, -10.0f))); // right of the right plane
    EXPECT_FALSE(f.intersects(at(-30.0f, 0.0f, -10.0f)));
    EXPECT_FALSE(f.intersects(at(0.0f, 30.0f, -10.0f))); // above the top plane
    EXPECT_FALSE(f.intersects(at(0.0f, -30.0f, -10.0f)));
}

TEST(frustum, culls_unit_boxes_against_an_orthographic_light_box)
{
    // A directional-light style box: eye at z = 20 looking down -Z, 5 units
    // either side, depth 0..40 (so the world spans z in [-20, 20]).
    mat4 view = look_at(vec3(0.0f, 0.0f, 20.0f), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
    mat4 proj = ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.0f, 40.0f);
    frustum f = frustum::from_view_projection(proj * view);
    const aabb unit(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 0.5f));
    const auto at = [&](float x, float y, float z) { return transform(unit, translate(vec3(x, y, z))); };

    EXPECT_TRUE(f.intersects(unit));                   // at the origin, mid depth
    EXPECT_TRUE(f.intersects(at(4.8f, 0.0f, 0.0f)));   // straddling the +X wall
    EXPECT_TRUE(f.intersects(at(0.0f, 0.0f, -19.8f))); // straddling the far plane

    EXPECT_FALSE(f.intersects(at(6.0f, 0.0f, 0.0f)));   // outside the +X wall
    EXPECT_FALSE(f.intersects(at(0.0f, -6.0f, 0.0f)));  // outside the -Y wall
    EXPECT_FALSE(f.intersects(at(0.0f, 0.0f, 21.0f)));  // behind the eye
    EXPECT_FALSE(f.intersects(at(0.0f, 0.0f, -25.0f))); // beyond the far plane
}
