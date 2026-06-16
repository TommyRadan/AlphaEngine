// Unit tests for rendering_engine::asset_cache: dedup by structural key,
// builder-runs-only-on-miss, collect_unused()/weak-ref semantics, and that the
// reference-counted mesh_asset releases its GPU buffers when the last handle
// drops. The asset layer resolves its device through asset_device(), so these
// run against a fake device with no engine, window, or backend present.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>

#include "support/fake_device.hpp"

namespace
{
    // A trivially-copyable vertex so mesh_data::from_vertices can interleave it.
    struct vertex
    {
        float x;
        float y;
        float z;
    };

    rendering_engine::mesh_data make_triangle()
    {
        const std::vector<vertex> verts{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        return rendering_engine::mesh_data::from_vertices(verts);
    }

    rendering_engine::mesh_data make_indexed_quad()
    {
        const std::vector<vertex> verts{
            {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
        const std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 3};
        return rendering_engine::mesh_data::from_vertices(verts, idx);
    }

    // Installs a fake device for the asset layer for the duration of each test.
    // Member order matters: the fake device outlives the cache, and the cache
    // (which owns only weak_ptrs) is torn down before it. Any shared asset
    // handle a test creates is a local that drops at the end of the test body —
    // before TearDown clears the device — so asset destructors always see it.
    class asset_cache_test : public ::testing::Test
    {
    protected:
        test_support::fake_device device;
        rendering_engine::asset_cache cache;

        void SetUp() override
        {
            rendering_engine::set_asset_device(&device);
        }
        void TearDown() override
        {
            rendering_engine::set_asset_device(nullptr);
        }
    };
}

TEST_F(asset_cache_test, builds_on_miss_and_uploads_once)
{
    int builds = 0;
    auto mesh = cache.get_or_create_mesh("triangle",
                                         [&]
                                         {
                                             ++builds;
                                             return make_triangle();
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(builds, 1);
    EXPECT_TRUE(mesh->vertex_buffer.valid());
    EXPECT_FALSE(mesh->index_buffer.valid()); // non-indexed
    EXPECT_EQ(mesh->vertex_count, 3u);
    EXPECT_EQ(mesh->vertex_stride, sizeof(vertex));
    EXPECT_EQ(device.created_buffers, 1u);
    EXPECT_EQ(cache.mesh_count(), 1u);
}

TEST_F(asset_cache_test, same_key_returns_the_shared_asset_without_rebuilding)
{
    int builds = 0;
    auto builder = [&]
    {
        ++builds;
        return make_triangle();
    };
    auto first = cache.get_or_create_mesh("triangle", builder);
    auto second = cache.get_or_create_mesh("triangle", builder);

    EXPECT_EQ(builds, 1);               // builder ran only on the miss
    EXPECT_EQ(first.get(), second.get()); // the same shared upload
    EXPECT_EQ(first.use_count(), 2);    // both handles share ownership
    EXPECT_EQ(device.created_buffers, 1u);
    EXPECT_EQ(cache.mesh_count(), 1u);
}

TEST_F(asset_cache_test, distinct_keys_produce_distinct_assets)
{
    auto a = cache.get_or_create_mesh("a", make_triangle);
    auto b = cache.get_or_create_mesh("b", make_triangle);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(cache.mesh_count(), 2u);
    EXPECT_EQ(device.created_buffers, 2u);
}

TEST_F(asset_cache_test, indexed_geometry_creates_an_index_buffer)
{
    auto mesh = cache.get_or_create_mesh("quad", make_indexed_quad);
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->vertex_buffer.valid());
    EXPECT_TRUE(mesh->index_buffer.valid());
    EXPECT_EQ(mesh->vertex_count, 4u);
    EXPECT_EQ(mesh->index_count, 6u);
    EXPECT_EQ(device.created_buffers, 2u); // vertex + index
}

TEST_F(asset_cache_test, dropping_the_last_handle_releases_gpu_buffers)
{
    {
        auto mesh = cache.get_or_create_mesh("quad", make_indexed_quad);
        EXPECT_EQ(device.live_buffer_count(), 2u);
    }
    // The asset destructor ran and freed both buffers through the device.
    EXPECT_EQ(device.live_buffer_count(), 0u);
    EXPECT_EQ(device.destroyed_buffers, 2u);
}

TEST_F(asset_cache_test, a_dropped_key_is_rebuilt_on_the_next_request)
{
    int builds = 0;
    auto builder = [&]
    {
        ++builds;
        return make_triangle();
    };

    rendering_engine::mesh_asset* first_address = nullptr;
    {
        auto mesh = cache.get_or_create_mesh("triangle", builder);
        first_address = mesh.get();
        EXPECT_EQ(cache.mesh_count(), 1u);
    }
    // Handle dropped: the weak entry is now expired.
    EXPECT_EQ(cache.mesh_count(), 0u);

    auto rebuilt = cache.get_or_create_mesh("triangle", builder);
    EXPECT_EQ(builds, 2);                      // rebuilt on the miss
    EXPECT_NE(rebuilt.get(), first_address);   // a fresh asset
    EXPECT_EQ(cache.mesh_count(), 1u);
}

TEST_F(asset_cache_test, collect_unused_sweeps_only_expired_entries)
{
    auto kept = cache.get_or_create_mesh("kept", make_triangle);
    {
        auto temporary = cache.get_or_create_mesh("temporary", make_triangle);
        EXPECT_EQ(cache.mesh_count(), 2u);
    }
    // One entry is now expired but still occupies a map slot until swept.
    EXPECT_EQ(cache.collect_unused(), 1u);
    EXPECT_EQ(cache.mesh_count(), 1u);
    // Sweeping again finds nothing to remove.
    EXPECT_EQ(cache.collect_unused(), 0u);
    // The live asset is untouched.
    ASSERT_NE(kept, nullptr);
    EXPECT_TRUE(kept->vertex_buffer.valid());
}
