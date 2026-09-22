// Unit tests for the vertex_format table in rendering_engine/mesh/vertex.hpp:
// every vertex struct maps to its format with the matching stride, the named
// strides pin the attribute offsets the built-in materials hard-code, the
// compatibility relation admits exactly the prefix layouts, and
// mesh_data::from_vertices records the format alongside the stride.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/mesh/vertex.hpp>

using rendering_engine::vertex_format;
using rendering_engine::vertex_format_compatible;
using rendering_engine::vertex_format_name;
using rendering_engine::vertex_format_of_v;
using rendering_engine::vertex_format_stride;

namespace
{
    // A record the engine has no struct for, as an importer might produce.
    struct importer_record
    {
        float x;
        float y;
        float z;
        std::uint32_t packed_normal;
    };

    const vertex_format named_formats[] = {
        vertex_format::position,
        vertex_format::position_color,
        vertex_format::position_uv,
        vertex_format::position_normal,
        vertex_format::position_color_normal,
        vertex_format::position_uv_normal,
        vertex_format::position_uv_normal_tangent,
    };
} // namespace

TEST(vertex_format, every_struct_maps_to_its_format_with_its_own_stride)
{
    using namespace rendering_engine;

    EXPECT_EQ(vertex_format_of_v<vertex_position>, vertex_format::position);
    EXPECT_EQ(vertex_format_of_v<vertex_position_color>, vertex_format::position_color);
    EXPECT_EQ(vertex_format_of_v<vertex_position_uv>, vertex_format::position_uv);
    EXPECT_EQ(vertex_format_of_v<vertex_position_normal>, vertex_format::position_normal);
    EXPECT_EQ(vertex_format_of_v<vertex_position_color_normal>, vertex_format::position_color_normal);
    EXPECT_EQ(vertex_format_of_v<vertex_position_uv_normal>, vertex_format::position_uv_normal);
    EXPECT_EQ(vertex_format_of_v<vertex_position_uv_normal_tangent>, vertex_format::position_uv_normal_tangent);

    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position>), sizeof(vertex_position));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_color>), sizeof(vertex_position_color));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_uv>), sizeof(vertex_position_uv));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_normal>), sizeof(vertex_position_normal));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_color_normal>),
              sizeof(vertex_position_color_normal));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_uv_normal>), sizeof(vertex_position_uv_normal));
    EXPECT_EQ(vertex_format_stride(vertex_format_of_v<vertex_position_uv_normal_tangent>),
              sizeof(vertex_position_uv_normal_tangent));
}

TEST(vertex_format, named_strides_pin_the_material_attribute_offsets)
{
    // The built-in materials hard-code position at 0, uv/colour at 12, normal
    // at 20 (after uv) or 24 (after colour) and tangent at 32; those offsets
    // only hold while the records stay tightly packed at these sizes.
    EXPECT_EQ(vertex_format_stride(vertex_format::position), 12u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_color), 24u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_uv), 20u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_normal), 24u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_color_normal), 36u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_uv_normal), 32u);
    EXPECT_EQ(vertex_format_stride(vertex_format::position_uv_normal_tangent), 48u);
    EXPECT_EQ(vertex_format_stride(vertex_format::custom), 0u);
}

TEST(vertex_format, an_unlisted_struct_is_custom)
{
    EXPECT_EQ(vertex_format_of_v<importer_record>, vertex_format::custom);
}

TEST(vertex_format, names_are_distinct_and_never_empty)
{
    std::set<std::string> names;
    for (const vertex_format format : named_formats)
    {
        const std::string name = vertex_format_name(format);
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(names.insert(name).second) << name << " is reused";
    }
    EXPECT_STREQ(vertex_format_name(vertex_format::custom), "custom");
    EXPECT_STREQ(vertex_format_name(vertex_format::position_uv_normal_tangent), "position_uv_normal_tangent");
}

TEST(vertex_format, every_named_format_is_compatible_with_itself)
{
    for (const vertex_format format : named_formats)
    {
        EXPECT_TRUE(vertex_format_compatible(format, format)) << vertex_format_name(format);
    }
}

TEST(vertex_format, a_wider_record_feeds_any_reader_of_its_prefix)
{
    // A position-only pipeline can read every named record.
    for (const vertex_format format : named_formats)
    {
        EXPECT_TRUE(vertex_format_compatible(format, vertex_format::position)) << vertex_format_name(format);
    }

    // Position + uv sits at the front of the uv-bearing records.
    EXPECT_TRUE(vertex_format_compatible(vertex_format::position_uv_normal, vertex_format::position_uv));
    EXPECT_TRUE(vertex_format_compatible(vertex_format::position_uv_normal_tangent, vertex_format::position_uv));

    // A phong-style position + uv + normal reader accepts the tangent record.
    EXPECT_TRUE(
        vertex_format_compatible(vertex_format::position_uv_normal_tangent, vertex_format::position_uv_normal));

    // Position + colour sits at the front of position + colour + normal.
    EXPECT_TRUE(vertex_format_compatible(vertex_format::position_color_normal, vertex_format::position_color));
}

TEST(vertex_format, a_narrower_record_is_rejected)
{
    // The bug from the issue: a tangent-reading pipeline over a 32-byte
    // position + uv + normal record would fetch 16 bytes past every vertex.
    EXPECT_FALSE(
        vertex_format_compatible(vertex_format::position_uv_normal, vertex_format::position_uv_normal_tangent));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_uv, vertex_format::position_uv_normal));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position, vertex_format::position_uv));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_color, vertex_format::position_color_normal));
}

TEST(vertex_format, a_record_with_a_different_channel_at_the_same_offset_is_rejected)
{
    // Same width, different meaning: colour is not uv.
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_color, vertex_format::position_uv));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_uv, vertex_format::position_color));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_normal, vertex_format::position_color));

    // The normal lives at offset 24 in position + colour + normal but a
    // position + normal reader expects it at 12.
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_color_normal, vertex_format::position_normal));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_uv_normal, vertex_format::position_normal));
    EXPECT_FALSE(vertex_format_compatible(vertex_format::position_uv_normal_tangent, vertex_format::position_normal));
}

TEST(vertex_format, custom_carries_no_channel_information)
{
    for (const vertex_format format : named_formats)
    {
        EXPECT_FALSE(vertex_format_compatible(vertex_format::custom, format)) << vertex_format_name(format);
        EXPECT_FALSE(vertex_format_compatible(format, vertex_format::custom)) << vertex_format_name(format);
    }
    EXPECT_FALSE(vertex_format_compatible(vertex_format::custom, vertex_format::custom));
}

TEST(mesh_data, from_vertices_records_the_format_and_stride_of_a_named_struct)
{
    using namespace rendering_engine;

    const std::vector<vertex_position_uv_normal_tangent> tangent_verts(3);
    const mesh_data tangent_mesh = mesh_data::from_vertices(tangent_verts, std::vector<std::uint32_t>{0, 1, 2});
    EXPECT_EQ(tangent_mesh.format, vertex_format::position_uv_normal_tangent);
    EXPECT_EQ(tangent_mesh.vertex_stride, sizeof(vertex_position_uv_normal_tangent));
    EXPECT_EQ(tangent_mesh.vertex_bytes.size(), 3 * sizeof(vertex_position_uv_normal_tangent));
    EXPECT_EQ(tangent_mesh.indices.size(), 3u);

    const std::vector<vertex_position_uv_normal> lit_verts(2);
    const mesh_data lit_mesh = mesh_data::from_vertices(lit_verts);
    EXPECT_EQ(lit_mesh.format, vertex_format::position_uv_normal);
    EXPECT_EQ(lit_mesh.vertex_stride, sizeof(vertex_position_uv_normal));

    const std::vector<vertex_position_color> line_verts(2);
    const mesh_data line_mesh = mesh_data::from_vertices(line_verts);
    EXPECT_EQ(line_mesh.format, vertex_format::position_color);
    EXPECT_EQ(line_mesh.vertex_stride, sizeof(vertex_position_color));
}

TEST(mesh_data, from_vertices_marks_an_unlisted_struct_custom_but_keeps_its_stride)
{
    using namespace rendering_engine;

    const std::vector<importer_record> verts(4);
    const mesh_data mesh = mesh_data::from_vertices(verts);
    EXPECT_EQ(mesh.format, vertex_format::custom);
    EXPECT_EQ(mesh.vertex_stride, sizeof(importer_record));
    EXPECT_EQ(mesh.vertex_bytes.size(), 4 * sizeof(importer_record));
}

TEST(mesh_data, a_default_mesh_data_is_custom_and_empty)
{
    const rendering_engine::mesh_data mesh;
    EXPECT_EQ(mesh.format, vertex_format::custom);
    EXPECT_EQ(mesh.vertex_stride, 0u);
    EXPECT_TRUE(mesh.vertex_bytes.empty());
}
