// Unit tests for rendering_engine::load_gltf: geometry (record format,
// stride, index widening, generated flat normals / tangents, bounds), the
// node tree (TRS, matrix decomposition, parents / children, roots, multi-
// primitive meshes), materials (factors, maps, the metallic-roughness split)
// through a recording gltf_material_factory, textures through the asset cache
// in the colour space their slot implies, and the three container forms:
// .gltf with data URIs, .gltf with external files, and .glb. Files are written
// to the temp directory; the asset layer runs against the fake device.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/assets/gltf_importer.hpp>
#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/util/image.hpp>

#include "support/fake_device.hpp"

namespace
{
    namespace math = core::math;
    using rendering_engine::gltf_npos;

    constexpr float k_eps = 1e-4f;

    // A 2x2 RGBA PNG whose texels are (10,20,30), (40,50,60) on the top row
    // and (70,80,90), (100,110,120) on the bottom, all opaque. Distinct R/G/B
    // per texel so the metallic (B) / roughness (G) split is observable.
    const std::vector<std::uint8_t> k_png{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00,
        0x00, 0x00, 0x1a, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xe0, 0x12, 0x91, 0xfb, 0xaf, 0x61, 0x64,
        0xf3, 0x9f, 0xc1, 0x2d, 0x20, 0xea, 0x7f, 0x4a, 0x5e, 0xc5, 0x7f, 0x00, 0x32, 0xda, 0x07, 0x09, 0x00,
        0x43, 0x31, 0x36, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

    std::string base64_encode(const std::vector<std::uint8_t>& bytes)
    {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        std::size_t i = 0;
        for (; i + 2 < bytes.size(); i += 3)
        {
            const std::uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
            out += alphabet[(triple >> 18) & 63];
            out += alphabet[(triple >> 12) & 63];
            out += alphabet[(triple >> 6) & 63];
            out += alphabet[triple & 63];
        }
        if (i + 1 == bytes.size())
        {
            const std::uint32_t triple = bytes[i] << 16;
            out += alphabet[(triple >> 18) & 63];
            out += alphabet[(triple >> 12) & 63];
            out += "==";
        }
        else if (i + 2 == bytes.size())
        {
            const std::uint32_t triple = (bytes[i] << 16) | (bytes[i + 1] << 8);
            out += alphabet[(triple >> 18) & 63];
            out += alphabet[(triple >> 12) & 63];
            out += alphabet[(triple >> 6) & 63];
            out += '=';
        }
        return out;
    }

    void append_f32(std::vector<std::uint8_t>& out, float value)
    {
        std::uint8_t bytes[sizeof(float)];
        std::memcpy(bytes, &value, sizeof(float));
        out.insert(out.end(), bytes, bytes + sizeof(float));
    }

    void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value & 0xff));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
        }
    }

    // One CCW triangle in the XY plane with normals, uvs and u16 indices:
    // positions @0 (36 bytes), normals @36 (36), uvs @72 (24), indices @96
    // (6) = 102 bytes.
    std::vector<std::uint8_t> triangle_bytes()
    {
        std::vector<std::uint8_t> bytes;
        const float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        const float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        const float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        for (float f : positions)
        {
            append_f32(bytes, f);
        }
        for (float f : normals)
        {
            append_f32(bytes, f);
        }
        for (float f : uvs)
        {
            append_f32(bytes, f);
        }
        append_u16(bytes, 0);
        append_u16(bytes, 1);
        append_u16(bytes, 2);
        return bytes;
    }

    // The accessor / bufferView tables for triangle_bytes() in buffer 0.
    const char* k_triangle_tables = R"(
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6}
  ])";

    const char* k_triangle_primitive = R"({"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3})";

    std::string data_uri(const std::vector<std::uint8_t>& bytes, const char* mime)
    {
        return std::string{"data:"} + mime + ";base64," + base64_encode(bytes);
    }

    std::string replace_all(std::string text, const std::string& from, const std::string& to)
    {
        for (std::size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
        {
            text.replace(pos, from.size(), to);
        }
        return text;
    }

    // A one-node scene drawing the triangle; @p extra is spliced in as
    // additional top-level members and @p buffer_uri names the buffer.
    std::string triangle_gltf(const std::string& buffer_uri,
                              const std::string& extra = "",
                              const std::string& primitive_extra = "")
    {
        std::string json = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "tri", "mesh": 0}],
  "meshes": [{"primitives": [@PRIMITIVE@]}],
  @TABLES@,
  "buffers": [{"byteLength": 102@BUFFER_URI@}]@EXTRA@
})";
        std::string primitive = k_triangle_primitive;
        if (!primitive_extra.empty())
        {
            primitive.insert(primitive.size() - 1, ", " + primitive_extra);
        }
        json = replace_all(json, "@PRIMITIVE@", primitive);
        json = replace_all(json, "@TABLES@", k_triangle_tables);
        json = replace_all(json, "@BUFFER_URI@", buffer_uri.empty() ? "" : ", \"uri\": \"" + buffer_uri + "\"");
        json = replace_all(json, "@EXTRA@", extra.empty() ? "" : ",\n  " + extra);
        return json;
    }

    struct recorded_material
    {
        std::string name;
        math::vec4 base_color_factor;
        float metallic_factor;
        float roughness_factor;
        math::vec3 emissive_factor;
        float emissive_strength;
        rendering_engine::gpu::color_space base_color_space;
        std::optional<rendering_engine::util::image> base_color_map;
        std::optional<rendering_engine::util::image> normal_map;
        std::optional<rendering_engine::util::image> metallic_map;
        std::optional<rendering_engine::util::image> roughness_map;
        std::optional<rendering_engine::util::image> emissive_map;
    };

    // Stands in for the renderer-backed factory: copies what the importer
    // hands over (the maps are only valid during the call) and creates
    // nothing, so the tests need no engine.
    struct recording_material_factory final : rendering_engine::gltf_material_factory
    {
        std::vector<recorded_material> records;

        std::shared_ptr<rendering_engine::standard_material>
        create(const rendering_engine::gltf_material_description& description) override
        {
            recorded_material record;
            record.name = description.name;
            record.base_color_factor = description.base_color_factor;
            record.metallic_factor = description.metallic_factor;
            record.roughness_factor = description.roughness_factor;
            record.emissive_factor = description.emissive_factor;
            record.emissive_strength = description.emissive_strength;
            record.base_color_space = description.base_color_space;
            const auto copy = [](const rendering_engine::util::image* image)
            { return image != nullptr ? std::optional{*image} : std::nullopt; };
            record.base_color_map = copy(description.base_color_map);
            record.normal_map = copy(description.normal_map);
            record.metallic_map = copy(description.metallic_map);
            record.roughness_map = copy(description.roughness_map);
            record.emissive_map = copy(description.emissive_map);
            records.push_back(std::move(record));
            return nullptr;
        }
    };

    // Fake device + cache for the asset layer, and a scratch directory the
    // glTF files are written to. Member order: the device outlives the cache.
    class gltf_importer_test : public ::testing::Test
    {
    protected:
        test_support::fake_device device;
        rendering_engine::asset_cache cache;
        recording_material_factory factory;
        std::filesystem::path dir;

        void SetUp() override
        {
            rendering_engine::set_asset_device(&device);
            static int counter = 0;
            dir = std::filesystem::temp_directory_path() / ("alphaengine_gltf_test_" + std::to_string(counter++));
            std::filesystem::create_directories(dir);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(dir, error);
            rendering_engine::set_asset_device(nullptr);
        }

        std::filesystem::path write(const std::string& name, const std::string& text)
        {
            const std::filesystem::path path = dir / name;
            std::ofstream out{path, std::ios::binary};
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return path;
        }

        std::filesystem::path write(const std::string& name, const std::vector<std::uint8_t>& bytes)
        {
            const std::filesystem::path path = dir / name;
            std::ofstream out{path, std::ios::binary};
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return path;
        }

        rendering_engine::gltf_model load(const std::filesystem::path& path,
                                          const rendering_engine::gltf_import_options& options = {})
        {
            return rendering_engine::load_gltf(path, cache, factory, options);
        }

        // The vertex records the cache uploaded for @p mesh, read back from
        // the fake device.
        std::vector<rendering_engine::vertex_position_uv_normal_tangent>
        vertices_of(const rendering_engine::mesh_asset& mesh)
        {
            const auto& bytes = device.buffer_contents.at(mesh.vertex_buffer.id);
            std::vector<rendering_engine::vertex_position_uv_normal_tangent> records(
                bytes.size() / sizeof(rendering_engine::vertex_position_uv_normal_tangent));
            std::memcpy(records.data(), bytes.data(), bytes.size());
            return records;
        }

        std::vector<std::uint32_t> indices_of(const rendering_engine::mesh_asset& mesh)
        {
            const auto& bytes = device.buffer_contents.at(mesh.index_buffer.id);
            std::vector<std::uint32_t> indices(bytes.size() / sizeof(std::uint32_t));
            std::memcpy(indices.data(), bytes.data(), bytes.size());
            return indices;
        }
    };

    void expect_vec3(const math::vec3& actual, float x, float y, float z)
    {
        EXPECT_NEAR(actual.x, x, k_eps);
        EXPECT_NEAR(actual.y, y, k_eps);
        EXPECT_NEAR(actual.z, z, k_eps);
    }
}

// -- geometry ----------------------------------------------------------------

TEST_F(gltf_importer_test, loads_a_triangle_from_a_data_uri)
{
    const auto path = write("triangle.gltf", triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream")));
    const auto model = load(path);

    ASSERT_EQ(model.primitives.size(), 1u);
    const auto& primitive = model.primitives[0];
    ASSERT_NE(primitive.mesh, nullptr);
    EXPECT_EQ(primitive.mesh->format, rendering_engine::vertex_format::position_uv_normal_tangent);
    EXPECT_EQ(primitive.mesh->vertex_stride, 48u);
    EXPECT_EQ(primitive.mesh->vertex_count, 3u);
    EXPECT_EQ(primitive.mesh->index_count, 3u);
    EXPECT_TRUE(primitive.mesh->index_buffer.valid());
    EXPECT_EQ(primitive.material_index, gltf_npos);

    // Bounds come from the accessor's min / max, forwarded to the cache.
    expect_vec3(primitive.mesh->bounds.min, 0.0f, 0.0f, 0.0f);
    expect_vec3(primitive.mesh->bounds.max, 1.0f, 1.0f, 0.0f);

    // The uploaded records carry the file's channels, the u16 indices are
    // widened to u32, and the tangent (generated: u runs along +X) is
    // right-handed.
    const auto records = vertices_of(*primitive.mesh);
    ASSERT_EQ(records.size(), 3u);
    expect_vec3(records[1].pos, 1.0f, 0.0f, 0.0f);
    expect_vec3(records[2].pos, 0.0f, 1.0f, 0.0f);
    EXPECT_NEAR(records[1].uv.x, 1.0f, k_eps);
    EXPECT_NEAR(records[2].uv.y, 1.0f, k_eps);
    expect_vec3(records[0].normal, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(records[0].tangent.x, 1.0f, k_eps);
    EXPECT_NEAR(records[0].tangent.w, 1.0f, k_eps);
    EXPECT_EQ(indices_of(*primitive.mesh), (std::vector<std::uint32_t>{0, 1, 2}));

    EXPECT_EQ(cache.mesh_count(), 1u);
    EXPECT_EQ(device.created_buffers, 2u);
}

TEST_F(gltf_importer_test, the_file_tangents_are_used_when_present)
{
    // Tangents as a fourth accessor: VEC4 @102 in a longer buffer.
    std::vector<std::uint8_t> bytes = triangle_bytes();
    bytes.push_back(0); // pad @102 to a 4-byte offset
    bytes.push_back(0);
    for (int v = 0; v < 3; ++v)
    {
        append_f32(bytes, 0.0f);
        append_f32(bytes, 1.0f);
        append_f32(bytes, 0.0f);
        append_f32(bytes, -1.0f);
    }
    std::string json = triangle_gltf(data_uri(bytes, "application/octet-stream"));
    json = replace_all(json, R"("TEXCOORD_0": 2})", R"("TEXCOORD_0": 2, "TANGENT": 4})");
    json = replace_all(json, R"({"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"})",
                       R"({"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC4"})");
    json = replace_all(json, R"({"buffer": 0, "byteOffset": 96, "byteLength": 6})",
                       R"({"buffer": 0, "byteOffset": 96, "byteLength": 6},
    {"buffer": 0, "byteOffset": 104, "byteLength": 48})");
    json = replace_all(json, "\"byteLength\": 102", "\"byteLength\": 152");

    const auto model = load(write("tangents.gltf", json));
    ASSERT_EQ(model.primitives.size(), 1u);
    const auto records = vertices_of(*model.primitives[0].mesh);
    ASSERT_EQ(records.size(), 3u);
    EXPECT_NEAR(records[0].tangent.y, 1.0f, k_eps);
    EXPECT_NEAR(records[0].tangent.w, -1.0f, k_eps);
}

TEST_F(gltf_importer_test, missing_normals_produce_flat_shaded_unwelded_vertices)
{
    // A CCW quad in the XY plane, positions @0 (48), uvs @48 (32), u16
    // indices @80 (12) = 92 bytes, with no NORMAL attribute.
    std::vector<std::uint8_t> bytes;
    const float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float uvs[] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    for (float f : positions)
    {
        append_f32(bytes, f);
    }
    for (float f : uvs)
    {
        append_f32(bytes, f);
    }
    const std::uint16_t quad_indices[] = {0, 1, 2, 0, 2, 3};
    for (std::uint16_t i : quad_indices)
    {
        append_u16(bytes, i);
    }
    const std::string json = R"({
  "asset": {"version": "2.0"},
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC2"},
    {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 48},
    {"buffer": 0, "byteOffset": 48, "byteLength": 32},
    {"buffer": 0, "byteOffset": 80, "byteLength": 12}
  ],
  "buffers": [{"byteLength": 92, "uri": ")" +
                             data_uri(bytes, "application/octet-stream") + R"("}]
})";

    const auto model = load(write("quad.gltf", json));
    ASSERT_EQ(model.primitives.size(), 1u);
    const auto& mesh = *model.primitives[0].mesh;
    // Every triangle owns its three vertices so each can carry the face normal.
    EXPECT_EQ(mesh.vertex_count, 6u);
    EXPECT_EQ(mesh.index_count, 6u);
    for (const auto& record : vertices_of(mesh))
    {
        expect_vec3(record.normal, 0.0f, 0.0f, 1.0f);
    }
    EXPECT_EQ(indices_of(mesh), (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}));
    // No scene index: the first scene's roots are used.
    EXPECT_EQ(model.root_nodes, (std::vector<std::size_t>{0}));
}

TEST_F(gltf_importer_test, non_triangle_primitives_are_skipped)
{
    // The same triangle twice, once as POINTS (mode 0) and once as TRIANGLES.
    std::string json = triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"));
    json = replace_all(json,
                       std::string{"["} + k_triangle_primitive + "]",
                       std::string{"["} + k_triangle_primitive + ", " +
                           replace_all(k_triangle_primitive, "\"indices\": 3", "\"indices\": 3, \"mode\": 0") + "]");

    const auto model = load(write("points.gltf", json));
    EXPECT_EQ(model.primitives.size(), 1u);
    ASSERT_EQ(model.nodes.size(), 1u);
    EXPECT_EQ(model.nodes[0].primitives, (std::vector<std::size_t>{0}));
}

TEST_F(gltf_importer_test, loading_the_same_file_twice_shares_the_cached_meshes)
{
    const auto path = write("shared.gltf", triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream")));
    const auto first = load(path);
    const auto uploads = device.created_buffers;
    const auto second = load(path);

    ASSERT_EQ(first.primitives.size(), 1u);
    ASSERT_EQ(second.primitives.size(), 1u);
    EXPECT_EQ(first.primitives[0].mesh.get(), second.primitives[0].mesh.get());
    EXPECT_EQ(device.created_buffers, uploads); // the builder did not run again
    EXPECT_EQ(cache.mesh_count(), 1u);
}

// -- nodes -------------------------------------------------------------------

TEST_F(gltf_importer_test, imports_the_node_tree_with_trs_and_matrix_poses)
{
    // root (TRS) -> child (two triangle primitives); a second root authored
    // as a matrix: uniform scale 2 and a translation, no rotation.
    std::string json = triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"));
    json = replace_all(json, R"("scenes": [{"nodes": [0]}])", R"("scenes": [{"nodes": [0, 2]}])");
    json = replace_all(json,
                       R"("nodes": [{"name": "tri", "mesh": 0}])",
                       R"("nodes": [
    {"name": "root", "children": [1], "translation": [1, 2, 3], "rotation": [0, 0, 0.7071068, 0.7071068], "scale": [2, 2, 2]},
    {"name": "child", "mesh": 0},
    {"name": "boxed", "mesh": 0, "matrix": [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 4, 5, 6, 1]}
  ])");
    json = replace_all(json,
                       std::string{"["} + k_triangle_primitive + "]",
                       std::string{"["} + k_triangle_primitive + ", " + k_triangle_primitive + "]");

    const auto model = load(write("tree.gltf", json));
    ASSERT_EQ(model.nodes.size(), 3u);
    EXPECT_EQ(model.root_nodes, (std::vector<std::size_t>{0, 2}));

    const auto& root = model.nodes[0];
    EXPECT_EQ(root.name, "root");
    EXPECT_EQ(root.parent, gltf_npos);
    EXPECT_EQ(root.children, (std::vector<std::size_t>{1}));
    EXPECT_TRUE(root.primitives.empty());
    expect_vec3(root.translation, 1.0f, 2.0f, 3.0f);
    expect_vec3(root.scale, 2.0f, 2.0f, 2.0f);
    // glTF (x, y, z, w) -> engine (w, x, y, z).
    EXPECT_NEAR(root.rotation.w, 0.7071068f, k_eps);
    EXPECT_NEAR(root.rotation.z, 0.7071068f, k_eps);
    EXPECT_NEAR(root.rotation.x, 0.0f, k_eps);

    const auto& child = model.nodes[1];
    EXPECT_EQ(child.parent, 0u);
    EXPECT_EQ(child.primitives.size(), 2u);
    EXPECT_EQ(model.primitives.size(), 2u);

    const auto& boxed = model.nodes[2];
    expect_vec3(boxed.translation, 4.0f, 5.0f, 6.0f);
    expect_vec3(boxed.scale, 2.0f, 2.0f, 2.0f);
    EXPECT_NEAR(boxed.rotation.w, 1.0f, k_eps);
    EXPECT_NEAR(boxed.rotation.x, 0.0f, k_eps);
    EXPECT_NEAR(boxed.rotation.y, 0.0f, k_eps);
    EXPECT_NEAR(boxed.rotation.z, 0.0f, k_eps);
}

// -- materials and textures --------------------------------------------------

TEST_F(gltf_importer_test, a_primitive_without_a_material_gets_the_default_material)
{
    load(write("default.gltf", triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"))));

    ASSERT_EQ(factory.records.size(), 1u);
    const auto& record = factory.records[0];
    EXPECT_EQ(record.name, "gltf default");
    EXPECT_NEAR(record.base_color_factor.x, 1.0f, k_eps);
    EXPECT_NEAR(record.base_color_factor.w, 1.0f, k_eps);
    EXPECT_NEAR(record.metallic_factor, 1.0f, k_eps);
    EXPECT_NEAR(record.roughness_factor, 1.0f, k_eps);
    EXPECT_FALSE(record.base_color_map.has_value());
}

TEST_F(gltf_importer_test, materials_carry_their_factors_and_maps_and_split_the_orm_texture)
{
    // One embedded PNG sampled by four textures in four slots.
    const std::string extra = R"("images": [{"uri": ")" + data_uri(k_png, "image/png") + R"("}],
  "textures": [{"source": 0}, {"source": 0}, {"source": 0}, {"source": 0}],
  "extensionsUsed": ["KHR_materials_emissive_strength"],
  "materials": [{
    "name": "painted",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.5, 0.25, 1.0, 0.75],
      "metallicFactor": 0.3,
      "roughnessFactor": 0.6,
      "baseColorTexture": {"index": 0},
      "metallicRoughnessTexture": {"index": 1}
    },
    "normalTexture": {"index": 2},
    "emissiveTexture": {"index": 3},
    "emissiveFactor": [0.1, 0.2, 0.3],
    "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 4.0}}
  }])";
    const auto model = load(write("painted.gltf",
                                  triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"),
                                                extra,
                                                "\"material\": 0")));

    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_EQ(model.primitives[0].material_index, 0u);
    EXPECT_EQ(model.materials.size(), 1u);
    EXPECT_EQ(model.default_material, nullptr); // nothing needed it

    ASSERT_EQ(factory.records.size(), 1u);
    const auto& record = factory.records[0];
    EXPECT_EQ(record.name, "painted");
    EXPECT_NEAR(record.base_color_factor.x, 0.5f, k_eps);
    EXPECT_NEAR(record.base_color_factor.y, 0.25f, k_eps);
    EXPECT_NEAR(record.base_color_factor.z, 1.0f, k_eps);
    EXPECT_NEAR(record.base_color_factor.w, 0.75f, k_eps);
    EXPECT_NEAR(record.metallic_factor, 0.3f, k_eps);
    EXPECT_NEAR(record.roughness_factor, 0.6f, k_eps);
    expect_vec3(record.emissive_factor, 0.1f, 0.2f, 0.3f);
    EXPECT_NEAR(record.emissive_strength, 4.0f, k_eps);
    EXPECT_EQ(record.base_color_space, rendering_engine::gpu::color_space::srgb);

    ASSERT_TRUE(record.base_color_map.has_value());
    EXPECT_EQ(record.base_color_map->get_width(), 2u);
    EXPECT_EQ(record.base_color_map->get_height(), 2u);
    EXPECT_EQ(record.base_color_map->get_pixel(0, 0).r, 10);
    EXPECT_EQ(record.base_color_map->get_pixel(1, 1).b, 120);
    ASSERT_TRUE(record.normal_map.has_value());
    ASSERT_TRUE(record.emissive_map.has_value());

    // glTF packs metalness in B and roughness in G; the material samples .r
    // of two separate maps, so the importer splits them out.
    ASSERT_TRUE(record.metallic_map.has_value());
    ASSERT_TRUE(record.roughness_map.has_value());
    EXPECT_EQ(record.metallic_map->get_pixel(0, 0).r, 30);
    EXPECT_EQ(record.metallic_map->get_pixel(1, 1).r, 120);
    EXPECT_EQ(record.roughness_map->get_pixel(0, 0).r, 20);
    EXPECT_EQ(record.roughness_map->get_pixel(1, 0).r, 50);
    EXPECT_EQ(record.metallic_map->get_pixel(0, 0).a, 255);

    // The cache textures are uploaded in the colour space their slot
    // implies: base colour / emissive as sRGB, data maps as linear — and the
    // one image lands in the cache once per space.
    ASSERT_EQ(model.textures.size(), 4u);
    for (const auto& texture : model.textures)
    {
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->width, 2u);
    }
    EXPECT_EQ(model.textures[0]->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    EXPECT_EQ(model.textures[1]->format, rendering_engine::gpu::texture_format::rgba8_unorm);
    EXPECT_EQ(model.textures[2]->format, rendering_engine::gpu::texture_format::rgba8_unorm);
    EXPECT_EQ(model.textures[3]->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    EXPECT_EQ(model.textures[0].get(), model.textures[3].get());
    EXPECT_EQ(model.textures[1].get(), model.textures[2].get());
    EXPECT_EQ(cache.texture_count(), 2u);
    EXPECT_EQ(device.created_textures, 2u);
}

TEST_F(gltf_importer_test, base_color_space_option_selects_the_albedo_upload_format)
{
    const std::string extra = R"("images": [{"uri": ")" + data_uri(k_png, "image/png") + R"("}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}])";
    rendering_engine::gltf_import_options options;
    options.base_color_space = rendering_engine::gpu::color_space::linear;
    const auto model = load(write("linear.gltf",
                                  triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"),
                                                extra,
                                                "\"material\": 0")),
                            options);

    ASSERT_EQ(model.textures.size(), 1u);
    ASSERT_NE(model.textures[0], nullptr);
    EXPECT_EQ(model.textures[0]->format, rendering_engine::gpu::texture_format::rgba8_unorm);
    ASSERT_EQ(factory.records.size(), 1u);
    EXPECT_EQ(factory.records[0].base_color_space, rendering_engine::gpu::color_space::linear);
}

TEST_F(gltf_importer_test, an_undecodable_image_is_skipped_without_failing_the_load)
{
    const std::vector<std::uint8_t> garbage{1, 2, 3, 4, 5, 6, 7, 8};
    const std::string extra = R"("images": [{"uri": ")" + data_uri(garbage, "image/png") + R"("}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}])";
    const auto model = load(write("garbage.gltf",
                                  triangle_gltf(data_uri(triangle_bytes(), "application/octet-stream"),
                                                extra,
                                                "\"material\": 0")));

    ASSERT_EQ(model.primitives.size(), 1u);
    ASSERT_EQ(model.textures.size(), 1u);
    EXPECT_EQ(model.textures[0], nullptr);
    ASSERT_EQ(factory.records.size(), 1u);
    EXPECT_FALSE(factory.records[0].base_color_map.has_value());
    EXPECT_EQ(device.created_textures, 0u);
}

// -- containers --------------------------------------------------------------

TEST_F(gltf_importer_test, resolves_external_buffer_and_image_files_beside_the_gltf)
{
    write("triangle.bin", triangle_bytes());
    write("albedo.png", k_png);
    const std::string extra = R"("images": [{"uri": "albedo.png"}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}])";
    const auto model = load(write("external.gltf", triangle_gltf("triangle.bin", extra, "\"material\": 0")));

    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_EQ(model.primitives[0].mesh->index_count, 3u);
    ASSERT_EQ(model.textures.size(), 1u);
    ASSERT_NE(model.textures[0], nullptr);
    EXPECT_EQ(model.textures[0]->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    // A file image is keyed on its path, shared with any other loader of it.
    EXPECT_EQ(cache.load_texture(dir / "albedo.png").get(), model.textures[0].get());
    ASSERT_EQ(factory.records.size(), 1u);
    ASSERT_TRUE(factory.records[0].base_color_map.has_value());
    EXPECT_EQ(factory.records[0].base_color_map->get_pixel(0, 0).g, 20);
}

TEST_F(gltf_importer_test, loads_a_glb_with_an_image_in_the_binary_chunk)
{
    // BIN chunk: the triangle (102 bytes, padded to 104) then the PNG.
    std::vector<std::uint8_t> bin = triangle_bytes();
    bin.resize(104, 0);
    const std::uint32_t png_offset = static_cast<std::uint32_t>(bin.size());
    bin.insert(bin.end(), k_png.begin(), k_png.end());
    const std::uint32_t bin_length = static_cast<std::uint32_t>(bin.size());
    while (bin.size() % 4 != 0)
    {
        bin.push_back(0);
    }

    std::string json = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "tri", "mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 24},
    {"buffer": 0, "byteOffset": 96, "byteLength": 6},
    {"buffer": 0, "byteOffset": @PNG_OFFSET@, "byteLength": @PNG_LENGTH@}
  ],
  "buffers": [{"byteLength": @BIN_LENGTH@}],
  "images": [{"bufferView": 4, "mimeType": "image/png"}],
  "textures": [{"source": 0}],
  "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}]
})";
    json = replace_all(json, "@PNG_OFFSET@", std::to_string(png_offset));
    json = replace_all(json, "@PNG_LENGTH@", std::to_string(k_png.size()));
    json = replace_all(json, "@BIN_LENGTH@", std::to_string(bin_length));
    while (json.size() % 4 != 0)
    {
        json.push_back(' ');
    }

    std::vector<std::uint8_t> glb;
    append_u32(glb, 0x46546c67u); // "glTF"
    append_u32(glb, 2u);
    append_u32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    append_u32(glb, static_cast<std::uint32_t>(json.size()));
    append_u32(glb, 0x4e4f534au); // "JSON"
    glb.insert(glb.end(), json.begin(), json.end());
    append_u32(glb, static_cast<std::uint32_t>(bin.size()));
    append_u32(glb, 0x004e4942u); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());

    const auto model = load(write("model.glb", glb));
    ASSERT_EQ(model.primitives.size(), 1u);
    EXPECT_EQ(model.primitives[0].mesh->vertex_count, 3u);
    EXPECT_EQ(model.primitives[0].mesh->index_count, 3u);
    ASSERT_EQ(model.textures.size(), 1u);
    ASSERT_NE(model.textures[0], nullptr);
    EXPECT_EQ(model.textures[0]->width, 2u);
    EXPECT_EQ(model.textures[0]->format, rendering_engine::gpu::texture_format::rgba8_srgb);
    ASSERT_EQ(factory.records.size(), 1u);
    ASSERT_TRUE(factory.records[0].base_color_map.has_value());
    EXPECT_EQ(factory.records[0].base_color_map->get_pixel(1, 0).r, 40);
}

// -- failures ----------------------------------------------------------------

TEST_F(gltf_importer_test, a_missing_or_malformed_file_throws)
{
    EXPECT_THROW(load(dir / "missing.gltf"), std::runtime_error);
    EXPECT_THROW(load(write("garbage.gltf", std::string{"not json at all"})), std::runtime_error);
    // Valid JSON whose buffer cannot be found.
    EXPECT_THROW(load(write("nobuffer.gltf", triangle_gltf("missing.bin"))), std::runtime_error);
    EXPECT_EQ(cache.mesh_count(), 0u);
}
