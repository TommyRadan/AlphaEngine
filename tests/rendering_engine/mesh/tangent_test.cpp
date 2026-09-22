// Unit tests for rendering_engine::generate_tangents: the tangent follows the
// UV u-gradient and is orthogonal to the normal, mirrored UVs flip the
// handedness sign, degenerate triangles contribute nothing, the input
// channels pass through untouched, and a shared vertex weights each
// neighbouring triangle by its corner angle.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace math = core::math;
using rendering_engine::generate_tangents;
using rendering_engine::vertex_position_uv_normal;
using rendering_engine::vertex_position_uv_normal_tangent;

namespace
{
    vertex_position_uv_normal make_vertex(const math::vec3& pos, const math::vec2& uv, const math::vec3& normal)
    {
        vertex_position_uv_normal v;
        v.pos = pos;
        v.uv = uv;
        v.normal = normal;
        return v;
    }

    const math::vec3 up{0.0f, 0.0f, 1.0f};

    // An XY-plane quad facing +Z. @p mirror_u flips u across the quad so the
    // texture is mirrored horizontally while the geometry stays put.
    std::vector<vertex_position_uv_normal> make_quad(bool mirror_u)
    {
        const auto u = [mirror_u](float x) { return mirror_u ? 1.0f - x : x; };
        return {
            make_vertex({0.0f, 0.0f, 0.0f}, {u(0.0f), 0.0f}, up),
            make_vertex({1.0f, 0.0f, 0.0f}, {u(1.0f), 0.0f}, up),
            make_vertex({1.0f, 1.0f, 0.0f}, {u(1.0f), 1.0f}, up),
            make_vertex({0.0f, 1.0f, 0.0f}, {u(0.0f), 1.0f}, up),
        };
    }

    const std::vector<std::uint32_t> quad_indices{0, 1, 2, 0, 2, 3};

    void expect_near(const math::vec3& actual, const math::vec3& expected, float tolerance = 1e-5f)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }

    math::vec3 xyz(const math::vec4& v)
    {
        return math::vec3{v.x, v.y, v.z};
    }
} // namespace

TEST(generate_tangents, tangent_follows_the_u_direction_with_positive_handedness)
{
    const auto out = generate_tangents(make_quad(false), quad_indices);
    ASSERT_EQ(out.size(), 4u);
    for (const auto& v : out)
    {
        // u increases along +X, v along +Y, normal +Z: a right-handed frame.
        expect_near(xyz(v.tangent), math::vec3{1.0f, 0.0f, 0.0f});
        EXPECT_FLOAT_EQ(v.tangent.w, 1.0f);
    }
}

TEST(generate_tangents, mirrored_uvs_flip_the_handedness_sign)
{
    const auto out = generate_tangents(make_quad(true), quad_indices);
    ASSERT_EQ(out.size(), 4u);
    for (const auto& v : out)
    {
        // u now decreases along +X, so the tangent points down -X while the
        // bitangent still runs along +Y: cross(normal, tangent) disagrees
        // with it, which the shader must undo via w = -1.
        expect_near(xyz(v.tangent), math::vec3{-1.0f, 0.0f, 0.0f});
        EXPECT_FLOAT_EQ(v.tangent.w, -1.0f);
    }
}

TEST(generate_tangents, output_is_unit_length_and_orthogonal_to_the_normal)
{
    // A tilted triangle whose UV gradient is not already in the surface
    // plane, so Gram-Schmidt has real work to do.
    const math::vec3 n = math::normalize(math::vec3{1.0f, 1.0f, 1.0f});
    const std::vector<vertex_position_uv_normal> verts{
        make_vertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, n),
        make_vertex({1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, n),
        make_vertex({0.0f, 1.0f, -1.0f}, {0.0f, 1.0f}, n),
    };
    const auto out = generate_tangents(verts, {0, 1, 2});
    ASSERT_EQ(out.size(), 3u);
    for (const auto& v : out)
    {
        const math::vec3 t = xyz(v.tangent);
        EXPECT_NEAR(math::length(t), 1.0f, 1e-5f);
        EXPECT_NEAR(math::dot(t, n), 0.0f, 1e-5f);
        EXPECT_TRUE(v.tangent.w == 1.0f || v.tangent.w == -1.0f);
    }
}

TEST(generate_tangents, position_uv_and_normal_pass_through_unchanged)
{
    const auto in = make_quad(false);
    const auto out = generate_tangents(in, quad_indices);
    ASSERT_EQ(out.size(), in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        expect_near(out[i].pos, in[i].pos);
        EXPECT_FLOAT_EQ(out[i].uv.x, in[i].uv.x);
        EXPECT_FLOAT_EQ(out[i].uv.y, in[i].uv.y);
        expect_near(out[i].normal, in[i].normal);
    }
}

TEST(generate_tangents, a_uv_degenerate_triangle_falls_back_to_a_stable_basis)
{
    // All three vertices share one UV, so there is no gradient to follow; the
    // frame must still be a valid unit tangent in the surface plane.
    const std::vector<vertex_position_uv_normal> verts{
        make_vertex({0.0f, 0.0f, 0.0f}, {0.5f, 0.5f}, up),
        make_vertex({1.0f, 0.0f, 0.0f}, {0.5f, 0.5f}, up),
        make_vertex({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, up),
    };
    const auto out = generate_tangents(verts, {0, 1, 2});
    ASSERT_EQ(out.size(), 3u);
    for (const auto& v : out)
    {
        const math::vec3 t = xyz(v.tangent);
        EXPECT_TRUE(std::isfinite(t.x) && std::isfinite(t.y) && std::isfinite(t.z));
        EXPECT_NEAR(math::length(t), 1.0f, 1e-5f);
        EXPECT_NEAR(math::dot(t, up), 0.0f, 1e-5f);
        EXPECT_FLOAT_EQ(v.tangent.w, 1.0f);
    }
}

TEST(generate_tangents, an_unreferenced_vertex_gets_a_basis_perpendicular_to_its_normal)
{
    // Vertex 3 is in no triangle. Its normal is +X, so the +X fallback axis
    // would collapse and the generator must pick +Y instead.
    std::vector<vertex_position_uv_normal> verts = make_quad(false);
    verts[3].normal = math::vec3{1.0f, 0.0f, 0.0f};
    const auto out = generate_tangents(verts, {0, 1, 2});
    ASSERT_EQ(out.size(), 4u);
    const math::vec3 t = xyz(out[3].tangent);
    EXPECT_NEAR(math::length(t), 1.0f, 1e-5f);
    EXPECT_NEAR(math::dot(t, verts[3].normal), 0.0f, 1e-5f);
}

TEST(generate_tangents, a_shared_vertex_weights_its_triangles_by_corner_angle)
{
    // Vertex 0 is shared by two coplanar triangles with perpendicular UV
    // gradients: triangle A (u along +X) meets vertex 0 at a right angle,
    // triangle B (u along +Y) at a sliver of ~5.7 degrees. Counting each
    // triangle once would average the two directions to ~45 degrees; angle
    // weighting keeps the frame dominated by the triangle that covers the
    // surface around the vertex.
    const std::vector<vertex_position_uv_normal> verts{
        make_vertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, up),  // 0: shared
        make_vertex({1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, up),  // 1: A, u = x
        make_vertex({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, up),  // 2: A, v = y
        make_vertex({-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, up), // 3: B, u = y, v = -x
        make_vertex({-1.0f, 0.1f, 0.0f}, {0.1f, 1.0f}, up), // 4: B
    };
    const auto out = generate_tangents(verts, {0, 1, 2, 0, 3, 4});
    ASSERT_EQ(out.size(), 5u);

    // Triangle A alone would give +X, triangle B alone +Y. Unweighted
    // accumulation lands near (0.71, 0.71); angle weighting (pi/2 vs ~0.1
    // radians) keeps it within a few degrees of +X while still showing B's
    // small contribution.
    const math::vec3 t = xyz(out[0].tangent);
    EXPECT_NEAR(math::length(t), 1.0f, 1e-5f);
    EXPECT_GT(t.x, 0.95f);
    EXPECT_GT(t.y, 0.0f);
    EXPECT_LT(t.y, 0.2f);
    EXPECT_NEAR(t.z, 0.0f, 1e-5f);

    // The unshared vertices follow their own triangle exactly.
    expect_near(xyz(out[1].tangent), math::vec3{1.0f, 0.0f, 0.0f});
    expect_near(xyz(out[3].tangent), math::vec3{0.0f, 1.0f, 0.0f});
}

TEST(generate_tangents, a_position_degenerate_triangle_contributes_nothing)
{
    // Triangle B collapses two of its vertices onto one point (as the pole
    // fan of a UV sphere does) while keeping distinct UVs, so its UV area
    // is non-zero but its corner angles are all zero: the shared vertex
    // must keep triangle A's frame exactly.
    const std::vector<vertex_position_uv_normal> verts{
        make_vertex({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, up),
        make_vertex({1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, up),
        make_vertex({0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, up),
        make_vertex({0.0f, 0.0f, 0.0f}, {0.5f, 0.0f}, up), // same position as 0
        make_vertex({-1.0f, 0.0f, 0.0f}, {0.5f, 1.0f}, up),
    };
    const auto out = generate_tangents(verts, {0, 1, 2, 0, 3, 4});
    ASSERT_EQ(out.size(), 5u);
    expect_near(xyz(out[0].tangent), math::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(out[0].tangent.w, 1.0f);
}

TEST(generate_tangents, an_empty_mesh_yields_an_empty_result)
{
    const auto out = generate_tangents({}, {});
    EXPECT_TRUE(out.empty());
}
