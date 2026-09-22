// Unit tests for rendering_engine::asset_cache: dedup by structural key,
// builder-runs-only-on-miss, collect_unused()/weak-ref semantics, that the
// reference-counted mesh_asset releases its GPU buffers when the last handle
// drops, and that load_texture asks the device for the texel format matching
// the requested colour space (sRGB by default) and keys the two spaces apart.
// The asset layer resolves its device through asset_device(), so these run
// against a fake device with no engine, window, or backend present.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <core/math/aabb.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/assets/texture_asset.hpp>
#include <rendering_engine/gpu/types.hpp>

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

TEST_F(asset_cache_test, an_unlisted_vertex_struct_is_cached_as_a_custom_format)
{
    auto mesh = cache.get_or_create_mesh("triangle", make_triangle);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->format, rendering_engine::vertex_format::custom);
    EXPECT_EQ(mesh->vertex_stride, sizeof(vertex));
}

TEST_F(asset_cache_test, a_named_vertex_struct_records_its_format_on_the_asset)
{
    auto mesh = cache.get_or_create_mesh("tangent_quad",
                                         []
                                         {
                                             const std::vector<rendering_engine::vertex_position_uv_normal_tangent>
                                                 verts(4);
                                             return rendering_engine::mesh_data::from_vertices(
                                                 verts, std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->format, rendering_engine::vertex_format::position_uv_normal_tangent);
    EXPECT_EQ(mesh->vertex_stride, sizeof(rendering_engine::vertex_position_uv_normal_tangent));
    EXPECT_EQ(mesh->vertex_count, 4u);
    EXPECT_EQ(mesh->index_count, 6u);
}

TEST_F(asset_cache_test, a_format_that_contradicts_the_stride_is_demoted_to_custom)
{
    // A builder claiming the 48-byte tangent record over 12-byte vertices
    // would let a tangent-reading material pass the format check and still
    // fetch past every vertex; the cache keeps only the stride it can trust.
    auto mesh = cache.get_or_create_mesh("misdeclared",
                                         []
                                         {
                                             rendering_engine::mesh_data data = make_triangle();
                                             data.format =
                                                 rendering_engine::vertex_format::position_uv_normal_tangent;
                                             return data;
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->format, rendering_engine::vertex_format::custom);
    EXPECT_EQ(mesh->vertex_stride, sizeof(vertex));
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

// -- Textures ----------------------------------------------------------------
//
// load_texture runs the real image decoder, so these tests write a 2x2 binary
// PPM (P6) — the simplest format stb_image reads — into the temp directory and
// load it through the cache against the fake device. What is under test is the
// colour-space plumbing: which texel format the cache asks the device for, that
// the format is recorded on the asset, and that the two colour spaces never
// alias in the cache.

namespace
{
    // The colour-space -> format mapping every RGBA8 upload site routes through.
    static_assert(rendering_engine::gpu::rgba8_format(rendering_engine::gpu::color_space::srgb) ==
                  rendering_engine::gpu::texture_format::rgba8_srgb);
    static_assert(rendering_engine::gpu::rgba8_format(rendering_engine::gpu::color_space::linear) ==
                  rendering_engine::gpu::texture_format::rgba8_unorm);

    class asset_cache_texture_test : public asset_cache_test
    {
    protected:
        std::filesystem::path image_path;

        void SetUp() override
        {
            asset_cache_test::SetUp();
            // One file per test (ctest may run tests in parallel processes).
            const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
            image_path = std::filesystem::temp_directory_path() /
                         (std::string{"alpha_engine_asset_cache_test_"} + info->name() + ".ppm");
            std::ofstream out{image_path, std::ios::binary};
            ASSERT_TRUE(out.is_open());
            out << "P6\n2 2\n255\n";
            const unsigned char texels[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 128, 128, 128};
            out.write(reinterpret_cast<const char*>(texels), sizeof(texels));
        }
        void TearDown() override
        {
            std::error_code ignored;
            std::filesystem::remove(image_path, ignored);
            asset_cache_test::TearDown();
        }
    };
}

TEST_F(asset_cache_texture_test, a_texture_loads_as_srgb_by_default)
{
    auto texture = cache.load_texture(image_path);
    ASSERT_NE(texture, nullptr);
    EXPECT_TRUE(texture->texture.valid());
    EXPECT_EQ(texture->width, 2u);
    EXPECT_EQ(texture->height, 2u);
    // The device was asked for the sRGB format, and the asset records it.
    EXPECT_EQ(device.created_textures, 1u);
    EXPECT_EQ(device.last_texture_descriptor.format, rendering_engine::gpu::texture_format::rgba8_srgb);
    EXPECT_TRUE(device.last_texture_descriptor.mipmaps);
    EXPECT_EQ(texture->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    EXPECT_EQ(cache.texture_count(), 1u);
}

TEST_F(asset_cache_texture_test, a_linear_request_loads_as_unorm)
{
    auto texture = cache.load_texture(image_path, rendering_engine::gpu::color_space::linear);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(device.created_textures, 1u);
    EXPECT_EQ(device.last_texture_descriptor.format, rendering_engine::gpu::texture_format::rgba8_unorm);
    EXPECT_EQ(texture->format, rendering_engine::gpu::texture_format::rgba8_unorm);
}

TEST_F(asset_cache_texture_test, the_same_file_in_the_same_color_space_is_shared)
{
    auto first = cache.load_texture(image_path, rendering_engine::gpu::color_space::srgb);
    auto second = cache.load_texture(image_path, rendering_engine::gpu::color_space::srgb);
    EXPECT_EQ(first.get(), second.get());
    EXPECT_EQ(device.created_textures, 1u); // decoded and uploaded once
    EXPECT_EQ(cache.texture_count(), 1u);
}

TEST_F(asset_cache_texture_test, the_same_file_in_two_color_spaces_is_two_assets)
{
    // An sRGB and a linear upload sample differently, so the colour space is
    // part of the key: neither request may be served the other's texture.
    auto srgb = cache.load_texture(image_path, rendering_engine::gpu::color_space::srgb);
    auto linear = cache.load_texture(image_path, rendering_engine::gpu::color_space::linear);
    ASSERT_NE(srgb, nullptr);
    ASSERT_NE(linear, nullptr);
    EXPECT_NE(srgb.get(), linear.get());
    EXPECT_EQ(srgb->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    EXPECT_EQ(linear->format, rendering_engine::gpu::texture_format::rgba8_unorm);
    EXPECT_EQ(device.created_textures, 2u);
    EXPECT_EQ(cache.texture_count(), 2u);
}

TEST_F(asset_cache_texture_test, dropping_the_last_handle_releases_the_gpu_texture)
{
    {
        auto texture = cache.load_texture(image_path);
        EXPECT_EQ(device.live_texture_count(), 1u);
    }
    EXPECT_EQ(device.live_texture_count(), 0u);
    EXPECT_EQ(device.destroyed_textures, 1u);
    EXPECT_EQ(cache.texture_count(), 0u);
}

// -- bounds ---------------------------------------------------------------------

TEST_F(asset_cache_test, computes_object_space_bounds_from_the_positions)
{
    auto mesh = cache.get_or_create_mesh("quad", make_indexed_quad);
    ASSERT_NE(mesh, nullptr);
    EXPECT_FLOAT_EQ(mesh->bounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.z, 0.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.y, 1.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.z, 0.0f);
}

TEST_F(asset_cache_test, bounds_read_the_leading_position_of_a_wide_record)
{
    // The 48-byte tangent record leads with its position like every named
    // format; the uv / normal / tangent bytes that follow must not be boxed.
    auto mesh = cache.get_or_create_mesh("tangent_tri",
                                         []
                                         {
                                             std::vector<rendering_engine::vertex_position_uv_normal_tangent> verts(3);
                                             verts[0].pos = core::math::vec3{-2.0f, 0.0f, 1.0f};
                                             verts[0].normal = core::math::vec3{50.0f, 50.0f, 50.0f};
                                             verts[1].pos = core::math::vec3{3.0f, -1.0f, 0.0f};
                                             verts[1].tangent = core::math::vec4{-50.0f, -50.0f, -50.0f, 1.0f};
                                             verts[2].pos = core::math::vec3{0.0f, 4.0f, -5.0f};
                                             return rendering_engine::mesh_data::from_vertices(verts);
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_FLOAT_EQ(mesh->bounds.min.x, -2.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.y, -1.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.z, -5.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.x, 3.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.y, 4.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.z, 1.0f);
}

TEST_F(asset_cache_test, a_builder_supplied_box_overrides_the_computed_bounds)
{
    // An importer whose record does not lead with the position hands the
    // cache its own box; the positions-at-offset-0 scan must not replace it.
    auto mesh = cache.get_or_create_mesh("custom_bounds",
                                         []
                                         {
                                             rendering_engine::mesh_data data = make_triangle();
                                             data.bounds = core::math::aabb{core::math::vec3{-9.0f, -8.0f, -7.0f},
                                                                            core::math::vec3{7.0f, 8.0f, 9.0f}};
                                             return data;
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_FLOAT_EQ(mesh->bounds.min.x, -9.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.y, -8.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.min.z, -7.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.x, 7.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.y, 8.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.z, 9.0f);
}

TEST_F(asset_cache_test, empty_geometry_leaves_a_zero_box)
{
    auto mesh = cache.get_or_create_mesh("empty",
                                         []
                                         {
                                             rendering_engine::mesh_data data;
                                             data.vertex_stride = sizeof(vertex);
                                             return data;
                                         });
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->vertex_count, 0u);
    EXPECT_FLOAT_EQ(mesh->bounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(mesh->bounds.max.x, 0.0f);
}

TEST(mesh_data_bounds, compute_bounds_walks_every_record_at_the_stride)
{
    // Position followed by a padding float: the padding must be stepped over,
    // never read as the next position.
    struct padded
    {
        float x;
        float y;
        float z;
        float pad;
    };
    const std::vector<padded> verts{{1.0f, 2.0f, 3.0f, 99.0f}, {-1.0f, 5.0f, 0.0f, 99.0f}, {0.0f, 0.0f, -7.0f, 99.0f}};
    const auto data = rendering_engine::mesh_data::from_vertices(verts);
    const auto bounds = data.compute_bounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_FLOAT_EQ(bounds->min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds->min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds->min.z, -7.0f);
    EXPECT_FLOAT_EQ(bounds->max.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds->max.y, 5.0f);
    EXPECT_FLOAT_EQ(bounds->max.z, 3.0f);

    // The raw entry point used by the private-upload renderables agrees.
    const auto raw = rendering_engine::compute_position_bounds(verts.data(), verts.size() * sizeof(padded), sizeof(padded));
    ASSERT_TRUE(raw.has_value());
    EXPECT_FLOAT_EQ(raw->min.z, -7.0f);
    EXPECT_FLOAT_EQ(raw->max.y, 5.0f);
}

TEST(mesh_data_bounds, no_bounds_for_empty_geometry_or_a_record_narrower_than_a_position)
{
    rendering_engine::mesh_data empty;
    empty.vertex_stride = sizeof(vertex);
    EXPECT_FALSE(empty.compute_bounds().has_value());

    rendering_engine::mesh_data narrow;
    narrow.vertex_stride = 8;
    narrow.vertex_bytes.resize(16);
    EXPECT_FALSE(narrow.compute_bounds().has_value());

    // A trailing partial record is ignored rather than read past the end.
    rendering_engine::mesh_data partial;
    partial.vertex_stride = sizeof(vertex);
    partial.vertex_bytes.resize(sizeof(vertex) - 1);
    EXPECT_FALSE(partial.compute_bounds().has_value());
}
