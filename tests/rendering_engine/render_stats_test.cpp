// Unit tests for rendering_engine/render_stats.hpp's tally_primitives: the
// per-topology primitive accounting the scene pass feeds the debug overlay.
// A triangle draw counts three vertices per triangle, a line draw two per
// segment, a point draw one per point, and a patch draw is not tallied; each
// lands in its own counter rather than every draw being divided by three.

#include <gtest/gtest.h>

#include <cstdint>

#include <rendering_engine/render_stats.hpp>

namespace
{
    using rendering_engine::render_stats;
    using rendering_engine::tally_primitives;
    namespace gpu = rendering_engine::gpu;
} // namespace

TEST(render_stats, triangles_count_three_vertices_each)
{
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::triangles, 36);
    EXPECT_EQ(stats.triangles, 12u);
    EXPECT_EQ(stats.lines, 0u);
    EXPECT_EQ(stats.points, 0u);
}

TEST(render_stats, lines_count_two_vertices_each)
{
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::lines, 36);
    EXPECT_EQ(stats.lines, 18u);
    EXPECT_EQ(stats.triangles, 0u);
    EXPECT_EQ(stats.points, 0u);
}

TEST(render_stats, points_count_one_vertex_each)
{
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::points, 36);
    EXPECT_EQ(stats.points, 36u);
    EXPECT_EQ(stats.triangles, 0u);
    EXPECT_EQ(stats.lines, 0u);
}

TEST(render_stats, patches_are_not_tallied)
{
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::patches, 36);
    EXPECT_EQ(stats.triangles, 0u);
    EXPECT_EQ(stats.lines, 0u);
    EXPECT_EQ(stats.points, 0u);
}

TEST(render_stats, incomplete_primitives_are_dropped)
{
    // A trailing vertex that does not complete a primitive is not drawn,
    // so it is not counted either.
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::triangles, 8);
    tally_primitives(stats, gpu::primitive_topology::lines, 5);
    EXPECT_EQ(stats.triangles, 2u);
    EXPECT_EQ(stats.lines, 2u);
}

TEST(render_stats, tallies_accumulate_across_draws)
{
    render_stats stats{};
    tally_primitives(stats, gpu::primitive_topology::triangles, 3);
    tally_primitives(stats, gpu::primitive_topology::triangles, 6);
    tally_primitives(stats, gpu::primitive_topology::lines, 4);
    tally_primitives(stats, gpu::primitive_topology::points, 7);
    tally_primitives(stats, gpu::primitive_topology::lines, 2);
    EXPECT_EQ(stats.triangles, 3u);
    EXPECT_EQ(stats.lines, 3u);
    EXPECT_EQ(stats.points, 7u);
}

TEST(render_stats, instanced_vertex_totals_tally_whole)
{
    // The caller multiplies an instanced draw through before tallying; a
    // 12-triangle mesh drawn 1000 times is 36000 vertices in, 12000 out.
    render_stats stats{};
    const uint64_t vertices = 36u * 1000u;
    tally_primitives(stats, gpu::primitive_topology::triangles, vertices);
    EXPECT_EQ(stats.triangles, 12000u);
}
