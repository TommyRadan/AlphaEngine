// Unit tests for the device-free half of the material system
// (rendering_engine/materials/pipeline_variant.hpp): the keyword -> define
// mapping, the variant key's packing and derivation from material_params,
// and the translation of a key onto depth / blend / rasterizer state.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>

#include <rendering_engine/materials/pipeline_variant.hpp>

using rendering_engine::all_material_keywords;
using rendering_engine::blend_mode;
using rendering_engine::keyword_bit;
using rendering_engine::keyword_define;
using rendering_engine::keyword_defines;
using rendering_engine::make_pipeline_variant_key;
using rendering_engine::material_keyword;
using rendering_engine::material_params;
using rendering_engine::pipeline_variant_key;
using rendering_engine::to_blend_state;
using rendering_engine::to_depth_state;
using rendering_engine::to_rasterizer_state;
namespace gpu = rendering_engine::gpu;

TEST(pipeline_variant, every_keyword_has_a_distinct_bit_and_define)
{
    std::set<uint32_t> bits;
    std::set<std::string> defines;
    for (const material_keyword keyword : all_material_keywords)
    {
        const uint32_t bit = keyword_bit(keyword);
        EXPECT_NE(bit, 0u);
        EXPECT_EQ(bit & (bit - 1u), 0u) << "keyword bits are single bits";
        bits.insert(bit);
        defines.insert(keyword_define(keyword));
    }
    EXPECT_EQ(bits.size(), all_material_keywords.size());
    EXPECT_EQ(defines.size(), all_material_keywords.size());
    EXPECT_STREQ(keyword_define(material_keyword::use_albedo_map), "USE_ALBEDO_MAP");
    EXPECT_STREQ(keyword_define(material_keyword::has_tangents), "HAS_TANGENTS");
    EXPECT_STREQ(keyword_define(material_keyword::wireframe), "WIREFRAME");
    EXPECT_STREQ(keyword_define(material_keyword::skinned), "SKINNED");
}

TEST(pipeline_variant, keyword_defines_emit_flags_for_set_bits_in_bit_order)
{
    const uint32_t mask = keyword_bit(material_keyword::use_normal_map) | keyword_bit(material_keyword::has_tangents) |
                          keyword_bit(material_keyword::use_orm_map);
    const gpu::shader_defines defines = keyword_defines(mask);
    ASSERT_EQ(defines.size(), 3u);
    EXPECT_EQ(defines[0].first, "USE_NORMAL_MAP");
    EXPECT_EQ(defines[1].first, "USE_ORM_MAP");
    EXPECT_EQ(defines[2].first, "HAS_TANGENTS");
    for (const auto& define : defines)
    {
        EXPECT_TRUE(define.second.empty()) << "keywords are flags";
    }

    EXPECT_TRUE(keyword_defines(0).empty());
}

TEST(pipeline_variant, keyword_defines_keep_the_template_base_defines_first)
{
    const gpu::shader_defines base = {{"GRID_FADE_DISTANCE", "42.0"}};
    const gpu::shader_defines defines = keyword_defines(keyword_bit(material_keyword::wireframe), base);
    ASSERT_EQ(defines.size(), 2u);
    EXPECT_EQ(defines[0].first, "GRID_FADE_DISTANCE");
    EXPECT_EQ(defines[0].second, "42.0");
    EXPECT_EQ(defines[1].first, "WIREFRAME");
}

TEST(pipeline_variant, key_from_default_params_is_opaque_back_culled_ccw_fill)
{
    const pipeline_variant_key key = make_pipeline_variant_key(material_params{}, 0);
    EXPECT_EQ(key.keywords, 0u);
    EXPECT_EQ(key.blending, blend_mode::none);
    EXPECT_EQ(key.cull, gpu::cull_mode::back);
    EXPECT_EQ(key.front, gpu::front_face::counter_clockwise);
    EXPECT_EQ(key.polygon, gpu::polygon_mode::fill);
    EXPECT_TRUE(key.depth_test);
    EXPECT_TRUE(key.depth_write);
}

TEST(pipeline_variant, key_folds_params_onto_fixed_function_fields)
{
    material_params params{};
    params.transparent = true;
    params.blending = blend_mode::additive;
    params.double_sided = true;
    params.wireframe = true;
    params.depth_test = false;
    params.depth_write = false;

    const uint32_t keywords = keyword_bit(material_keyword::use_albedo_map);
    const pipeline_variant_key key = make_pipeline_variant_key(params, keywords, gpu::front_face::clockwise);
    EXPECT_EQ(key.keywords, keywords | keyword_bit(material_keyword::wireframe));
    EXPECT_EQ(key.blending, blend_mode::additive);
    EXPECT_EQ(key.cull, gpu::cull_mode::none);
    EXPECT_EQ(key.front, gpu::front_face::clockwise);
    EXPECT_EQ(key.polygon, gpu::polygon_mode::line);
    EXPECT_FALSE(key.depth_test);
    EXPECT_FALSE(key.depth_write);
}

TEST(pipeline_variant, blending_only_counts_when_transparent)
{
    material_params params{};
    params.transparent = false;
    params.blending = blend_mode::multiply;
    EXPECT_EQ(make_pipeline_variant_key(params, 0).blending, blend_mode::none);

    params.transparent = true;
    EXPECT_EQ(make_pipeline_variant_key(params, 0).blending, blend_mode::multiply);
}

TEST(pipeline_variant, wireframe_keyword_follows_the_param_not_the_caller)
{
    // A stale WIREFRAME bit handed in by the caller is dropped when the
    // param is off, and added when it is on, so the keyword and the
    // polygon mode can never disagree.
    material_params params{};
    const uint32_t stale = keyword_bit(material_keyword::wireframe) | keyword_bit(material_keyword::has_tangents);
    EXPECT_EQ(make_pipeline_variant_key(params, stale).keywords, keyword_bit(material_keyword::has_tangents));

    params.wireframe = true;
    EXPECT_EQ(make_pipeline_variant_key(params, 0).keywords, keyword_bit(material_keyword::wireframe));
}

TEST(pipeline_variant, opacity_is_not_part_of_the_key)
{
    material_params a{};
    material_params b{};
    b.opacity = 0.25f;
    EXPECT_EQ(make_pipeline_variant_key(a, 0), make_pipeline_variant_key(b, 0));
    EXPECT_EQ(make_pipeline_variant_key(a, 0).pack(), make_pipeline_variant_key(b, 0).pack());
}

TEST(pipeline_variant, packed_keys_are_distinct_per_field)
{
    const pipeline_variant_key base{};
    std::set<uint64_t> packed{base.pack()};

    pipeline_variant_key k = base;
    k.keywords = 0xFFFFFFFFu;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    EXPECT_EQ(k.pack() & 0xFFFFFFFFu, 0xFFFFFFFFu) << "keywords occupy the low 32 bits";

    k = base;
    k.blending = blend_mode::subtractive;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    k = base;
    k.cull = gpu::cull_mode::none;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    k = base;
    k.front = gpu::front_face::clockwise;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    k = base;
    k.polygon = gpu::polygon_mode::line;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    k = base;
    k.depth_test = false;
    EXPECT_TRUE(packed.insert(k.pack()).second);
    k = base;
    k.depth_write = false;
    EXPECT_TRUE(packed.insert(k.pack()).second);

    // Equal keys pack equal.
    EXPECT_EQ(pipeline_variant_key{}.pack(), base.pack());
}

TEST(pipeline_variant, depth_state_is_encoded_once)
{
    pipeline_variant_key key{};
    key.depth_test = true;
    key.depth_write = false;
    gpu::depth_state depth = to_depth_state(key);
    EXPECT_TRUE(depth.test_enabled);
    EXPECT_FALSE(depth.write_enabled);
    EXPECT_EQ(depth.compare, gpu::compare_function::less);

    // Disabling the test flips the enable only; the comparison stays
    // the same rather than doubling up as compare == always.
    key.depth_test = false;
    depth = to_depth_state(key);
    EXPECT_FALSE(depth.test_enabled);
    EXPECT_EQ(depth.compare, gpu::compare_function::less);
}

TEST(pipeline_variant, blend_presets_map_onto_their_factors)
{
    EXPECT_FALSE(to_blend_state(blend_mode::none).enabled);

    const gpu::blend_state normal = to_blend_state(blend_mode::normal);
    EXPECT_TRUE(normal.enabled);
    EXPECT_EQ(normal.op, gpu::blend_op::add);
    EXPECT_EQ(normal.src, gpu::blend_factor::src_alpha);
    EXPECT_EQ(normal.dst, gpu::blend_factor::one_minus_src_alpha);

    const gpu::blend_state additive = to_blend_state(blend_mode::additive);
    EXPECT_EQ(additive.op, gpu::blend_op::add);
    EXPECT_EQ(additive.src, gpu::blend_factor::src_alpha);
    EXPECT_EQ(additive.dst, gpu::blend_factor::one);

    // Subtractive really subtracts: dst - src * alpha.
    const gpu::blend_state subtractive = to_blend_state(blend_mode::subtractive);
    EXPECT_TRUE(subtractive.enabled);
    EXPECT_EQ(subtractive.op, gpu::blend_op::reverse_subtract);
    EXPECT_EQ(subtractive.src, gpu::blend_factor::src_alpha);
    EXPECT_EQ(subtractive.dst, gpu::blend_factor::one);

    const gpu::blend_state multiply = to_blend_state(blend_mode::multiply);
    EXPECT_EQ(multiply.op, gpu::blend_op::add);
    EXPECT_EQ(multiply.src, gpu::blend_factor::zero);
    EXPECT_EQ(multiply.dst, gpu::blend_factor::src_color);
}

TEST(pipeline_variant, rasterizer_state_takes_the_key_verbatim)
{
    pipeline_variant_key key{};
    key.cull = gpu::cull_mode::none;
    key.front = gpu::front_face::clockwise;
    key.polygon = gpu::polygon_mode::line;
    const gpu::rasterizer_state rasterizer = to_rasterizer_state(key);
    EXPECT_EQ(rasterizer.cull, gpu::cull_mode::none);
    EXPECT_EQ(rasterizer.front, gpu::front_face::clockwise);
    EXPECT_EQ(rasterizer.polygon, gpu::polygon_mode::line);
}
