// Unit tests for rendering_engine::mesh: the CPU-side vertex store behind
// model::upload_mesh. Covers that an empty mesh reports zero vertices and that
// its vertices() accessor is safe to call (it used to index element 0 of an
// empty vector), and that upload_obj stores a copy of the caller's vertices.

#include <gtest/gtest.h>

#include <vector>

#include <rendering_engine/mesh/mesh.hpp>
#include <rendering_engine/mesh/vertex.hpp>

TEST(mesh, empty_mesh_reports_no_vertices)
{
    const rendering_engine::mesh empty;
    EXPECT_EQ(empty.vertex_count(), 0u);
    // Nothing to dereference; the call itself must be well-defined.
    (void)empty.vertices();
}

TEST(mesh, upload_obj_stores_a_copy_of_the_vertices)
{
    std::vector<rendering_engine::vertex_position_uv_normal> verts(3);
    verts[0].pos = core::math::vec3{0.0f, 0.0f, 0.0f};
    verts[1].pos = core::math::vec3{1.0f, 0.0f, 0.0f};
    verts[2].pos = core::math::vec3{0.0f, 1.0f, 0.0f};

    rendering_engine::mesh mesh;
    mesh.upload_obj(verts);

    ASSERT_EQ(mesh.vertex_count(), 3u);
    ASSERT_NE(mesh.vertices(), nullptr);
    EXPECT_NE(mesh.vertices(), verts.data());
    EXPECT_FLOAT_EQ(mesh.vertices()[1].pos.x, 1.0f);
    EXPECT_FLOAT_EQ(mesh.vertices()[2].pos.y, 1.0f);
}
