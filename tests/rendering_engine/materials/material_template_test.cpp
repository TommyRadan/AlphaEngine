// Unit tests for material_template + material over a fake device: N
// instances of one template share one pipeline, a parameter change
// rebinds through the variant cache (built once, then hits), a mirrored
// draw resolves the clockwise-front-face twin, map keywords compile a new
// shader set and the tangent keyword changes the vertex layout the
// pipeline is built with. The standard shaders are compiled through the
// real glslang (the test binary links the embedded shader registry), so
// a keyword that breaks the GLSL fails here, headless.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/materials/pipeline_variant.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <rendering_engine/util/color.hpp>
#include <rendering_engine/util/image.hpp>

#include "support/fake_device.hpp"

namespace
{
    namespace gpu = rendering_engine::gpu;
    using rendering_engine::blend_mode;
    using rendering_engine::keyword_bit;
    using rendering_engine::line_material;
    using rendering_engine::material_keyword;
    using rendering_engine::material_params;
    using rendering_engine::material_template;
    using rendering_engine::standard_material;
    using rendering_engine::ui_material;
    using rendering_engine::vertex_format;

    // Stands in for the scene pass's per-frame layout handle.
    constexpr gpu::bind_group_layout frame_layout{77};

    class material_template_test : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Keep the SPIR-V disk cache out of the picture.
            gpu::set_shader_cache_directory({});
        }

        void TearDown() override
        {
            gpu::set_shader_cache_directory({});
        }

        std::shared_ptr<material_template> standard_template()
        {
            return standard_material::create_template(device, frame_layout);
        }

        static bool has_attribute(const gpu::pipeline_descriptor& descriptor, uint32_t location)
        {
            for (const auto& attribute : descriptor.vertex_buffers.front().attributes)
            {
                if (attribute.location == location)
                {
                    return true;
                }
            }
            return false;
        }

        test_support::fake_device device;
    };

    rendering_engine::util::image tiny_image()
    {
        return rendering_engine::util::image{2, 2, rendering_engine::util::color{255, 255, 255, 255}};
    }
} // namespace

TEST_F(material_template_test, template_creates_layouts_but_no_pipeline_up_front)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    EXPECT_EQ(device.created_bind_group_layouts, 2u) << "per-draw + per-material";
    EXPECT_EQ(device.created_pipelines, 0u);
    EXPECT_EQ(device.created_shader_modules, 0u);
    EXPECT_EQ(tmpl->variant_count(), 0u);
    EXPECT_TRUE(tmpl->has_frame_layout());
    EXPECT_TRUE(tmpl->has_material_layout());
    EXPECT_EQ(tmpl->per_draw_slot(), 1u);
    EXPECT_EQ(tmpl->per_material_slot(), 2u);
}

TEST_F(material_template_test, n_instances_with_the_same_keywords_share_one_pipeline)
{
    const std::shared_ptr<material_template> tmpl = standard_template();

    std::vector<std::unique_ptr<standard_material>> instances;
    for (int i = 0; i < 8; ++i)
    {
        instances.push_back(std::make_unique<standard_material>(tmpl));
        instances.back()->set_base_color({static_cast<uint8_t>(i * 30), 0, 0, 255});
        instances.back()->set_roughness(0.1f * static_cast<float>(i));
    }

    EXPECT_EQ(device.created_pipelines, 1u);
    EXPECT_EQ(device.created_shader_modules, 2u) << "one vertex + one fragment module for the keyword set";
    EXPECT_EQ(tmpl->variant_count(), 1u);
    EXPECT_EQ(tmpl->shader_set_count(), 1u);
    EXPECT_EQ(tmpl->instances().size(), 8u);
    for (const auto& instance : instances)
    {
        EXPECT_EQ(instance->pipeline().id, instances.front()->pipeline().id);
        EXPECT_TRUE(instance->per_material_bind_group().valid()) << "each instance still owns its own bind group";
    }
    // Every instance has its own parameter UBO.
    EXPECT_EQ(device.live_buffer_count(), 8u);

    instances.pop_back();
    EXPECT_EQ(tmpl->instances().size(), 7u);
    instances.clear();
    EXPECT_EQ(tmpl->instances().size(), 0u);
    EXPECT_EQ(device.live_buffer_count(), 0u);
    EXPECT_EQ(device.live_pipeline_count(), 1u) << "pipelines belong to the template, not the instances";
}

TEST_F(material_template_test, template_releases_its_pipelines_and_shaders)
{
    {
        const std::shared_ptr<material_template> tmpl = standard_template();
        const standard_material a{tmpl};
        EXPECT_EQ(device.live_pipeline_count(), 1u);
    }
    EXPECT_EQ(device.live_pipeline_count(), 0u);
    EXPECT_EQ(device.destroyed_shader_modules, 2u);
    EXPECT_EQ(device.destroyed_bind_group_layouts, 2u);
}

TEST_F(material_template_test, a_params_change_rebinds_through_the_cache)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    standard_material a{tmpl};
    standard_material b{tmpl};
    const uint64_t opaque = a.pipeline().id;
    ASSERT_EQ(device.created_pipelines, 1u);

    // A key-changing param builds a second variant (same shaders).
    a.set_transparent(true);
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_EQ(device.created_shader_modules, 2u) << "fixed-function-only variants share the SPIR-V";
    EXPECT_NE(a.pipeline().id, opaque);
    EXPECT_EQ(b.pipeline().id, opaque);
    EXPECT_EQ(a.variant_key().blending, blend_mode::normal);
    EXPECT_TRUE(device.pipeline_descriptors.back().blend.enabled);

    // Another instance taking the same key is a cache hit.
    b.set_transparent(true);
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_EQ(b.pipeline().id, a.pipeline().id);

    // And going back is a hit too.
    a.set_transparent(false);
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_EQ(a.pipeline().id, opaque);

    // Opacity is a UBO value: no rebind at all.
    a.set_opacity(0.5f);
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_EQ(a.params().opacity, 0.5f);
    EXPECT_EQ(a.pipeline().id, opaque);

    // Wireframe, double-sided and the depth flags are each a key change.
    a.set_wireframe(true);
    EXPECT_EQ(device.pipeline_descriptors.back().rasterizer.polygon, gpu::polygon_mode::line);
    EXPECT_NE(a.variant_key().keywords & keyword_bit(material_keyword::wireframe), 0u)
        << "the WIREFRAME define rides along with the polygon mode";
    EXPECT_EQ(a.keywords() & keyword_bit(material_keyword::wireframe), 0u)
        << "the instance's own keyword set carries only its map / tangent bits";
    EXPECT_EQ(device.created_shader_modules, 4u) << "WIREFRAME is a new keyword set";
    a.set_double_sided(true);
    EXPECT_EQ(device.pipeline_descriptors.back().rasterizer.cull, gpu::cull_mode::none);
    a.set_depth_write(false);
    EXPECT_FALSE(device.pipeline_descriptors.back().depth.write_enabled);
    a.set_depth_test(false);
    EXPECT_FALSE(device.pipeline_descriptors.back().depth.test_enabled);
    EXPECT_EQ(device.created_pipelines, 6u);
}

TEST_F(material_template_test, mirrored_draws_resolve_the_clockwise_twin_lazily)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    standard_material a{tmpl};
    const gpu::pipeline ccw = a.pipeline();
    ASSERT_EQ(device.created_pipelines, 1u);
    EXPECT_EQ(device.pipeline_descriptors.back().rasterizer.front, gpu::front_face::counter_clockwise);

    EXPECT_EQ(a.pipeline(false).id, ccw.id);
    EXPECT_EQ(device.created_pipelines, 1u) << "no twin until a mirrored draw asks";

    const gpu::pipeline cw = a.pipeline(true);
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_NE(cw.id, ccw.id);
    EXPECT_EQ(device.pipeline_descriptors.back().rasterizer.front, gpu::front_face::clockwise);
    EXPECT_EQ(device.pipeline_descriptors.back().rasterizer.cull, gpu::cull_mode::back)
        << "still culls back faces, just with the other winding";

    EXPECT_EQ(a.pipeline(true).id, cw.id);
    EXPECT_EQ(device.created_pipelines, 2u) << "cached on the instance";

    // A second instance gets the same twin from the template cache.
    standard_material b{tmpl};
    EXPECT_EQ(b.pipeline(true).id, cw.id);
    EXPECT_EQ(device.created_pipelines, 2u);

    // A key change drops the twin; the next mirrored draw re-resolves it.
    a.set_double_sided(true);
    EXPECT_EQ(device.created_pipelines, 3u);
    EXPECT_NE(a.pipeline(true).id, cw.id);
    EXPECT_EQ(device.created_pipelines, 4u);
}

TEST_F(material_template_test, binding_a_map_compiles_a_keyword_variant_and_clearing_returns_to_the_cached_one)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    standard_material a{tmpl};
    const uint64_t plain = a.pipeline().id;
    EXPECT_EQ(a.keywords(), keyword_bit(material_keyword::has_tangents));

    const rendering_engine::util::image image = tiny_image();
    a.set_albedo_map(image);
    EXPECT_NE(a.keywords() & keyword_bit(material_keyword::use_albedo_map), 0u);
    EXPECT_EQ(device.created_shader_modules, 4u) << "a new keyword set is a new SPIR-V pair";
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_NE(a.pipeline().id, plain);

    a.set_normal_map(image);
    EXPECT_NE(a.keywords() & keyword_bit(material_keyword::use_normal_map), 0u);
    EXPECT_EQ(tmpl->shader_set_count(), 3u);

    a.clear_normal_map();
    a.clear_albedo_map();
    EXPECT_EQ(a.pipeline().id, plain);
    EXPECT_EQ(device.created_pipelines, 3u) << "clearing hits the cache";
    EXPECT_EQ(device.live_texture_count(), 0u);
}

TEST_F(material_template_test, orm_map_supersedes_the_metallic_and_roughness_maps_but_not_occlusion)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    standard_material a{tmpl};
    const rendering_engine::util::image image = tiny_image();

    a.set_metalness_map(image);
    a.set_roughness_map(image);
    a.set_occlusion_map(image);
    const uint32_t separate =
        keyword_bit(material_keyword::use_metallic_map) | keyword_bit(material_keyword::use_roughness_map);
    const uint32_t occlusion = keyword_bit(material_keyword::use_occlusion_map);
    EXPECT_EQ(a.keywords() & separate, separate);
    EXPECT_NE(a.keywords() & occlusion, 0u);
    EXPECT_FALSE(a.uses_orm_map());

    a.set_orm_map(image);
    EXPECT_TRUE(a.uses_orm_map());
    EXPECT_EQ(a.keywords() & separate, 0u) << "one packed variant replaces the two single-channel keywords";
    EXPECT_NE(a.keywords() & occlusion, 0u) << "a separate occlusion map still overrides the packed R";

    a.clear_occlusion_map();
    EXPECT_EQ(a.keywords() & occlusion, 0u) << "without it the packed R is the occlusion source";
    EXPECT_TRUE(a.uses_orm_map());

    a.clear_orm_map();
    EXPECT_FALSE(a.uses_orm_map());
    EXPECT_EQ(a.keywords() & separate, separate);
}

TEST_F(material_template_test, normal_map_needs_tangents_and_tangents_change_the_vertex_layout)
{
    const std::shared_ptr<material_template> tmpl = standard_template();
    standard_material a{tmpl};
    EXPECT_TRUE(a.has_tangents());
    EXPECT_EQ(a.required_vertex_format(), vertex_format::position_uv_normal_tangent);
    EXPECT_EQ(a.min_vertex_stride(), 48u);
    EXPECT_TRUE(has_attribute(device.pipeline_descriptors.back(), 3));

    const rendering_engine::util::image image = tiny_image();
    a.set_normal_map(image);
    EXPECT_NE(a.keywords() & keyword_bit(material_keyword::use_normal_map), 0u);

    a.set_tangents(false);
    EXPECT_FALSE(a.has_tangents());
    EXPECT_EQ(a.keywords() & keyword_bit(material_keyword::use_normal_map), 0u)
        << "no tangent frame, no normal mapping";
    EXPECT_EQ(a.required_vertex_format(), vertex_format::position_uv_normal);
    EXPECT_EQ(a.min_vertex_stride(), 32u);
    EXPECT_FALSE(has_attribute(device.pipeline_descriptors.back(), 3))
        << "the tangent-less variant must not fetch location 3";

    a.set_tangents(true);
    EXPECT_NE(a.keywords() & keyword_bit(material_keyword::use_normal_map), 0u);
    EXPECT_EQ(a.required_vertex_format(), vertex_format::position_uv_normal_tangent);
}

TEST_F(material_template_test, line_instances_with_different_depth_params_are_two_variants_of_one_template)
{
    const std::shared_ptr<material_template> tmpl = line_material::create_template(device, frame_layout);
    line_material scene{tmpl};
    line_material debug{tmpl, /*depth_tested=*/false};
    EXPECT_EQ(device.created_pipelines, 2u);
    EXPECT_EQ(device.created_shader_modules, 2u);
    EXPECT_NE(scene.pipeline().id, debug.pipeline().id);
    EXPECT_TRUE(scene.params().depth_test);
    EXPECT_FALSE(debug.params().depth_test);
    EXPECT_EQ(device.pipeline_descriptors.front().topology, gpu::primitive_topology::lines);
    EXPECT_EQ(scene.per_material_slot(), 2u);

    line_material another{tmpl};
    EXPECT_EQ(another.pipeline().id, scene.pipeline().id);
    EXPECT_EQ(device.created_pipelines, 2u);
}

TEST_F(material_template_test, ui_template_without_a_frame_layout_puts_per_draw_at_slot_zero)
{
    const std::shared_ptr<material_template> tmpl = ui_material::create_template(device);
    ui_material ui{tmpl};
    EXPECT_FALSE(tmpl->has_frame_layout());
    EXPECT_FALSE(tmpl->has_material_layout());
    EXPECT_EQ(ui.per_draw_slot(), 0u);
    EXPECT_EQ(ui.per_material_slot(), 1u);
    EXPECT_FALSE(ui.per_material_bind_group().valid());
    EXPECT_TRUE(ui.params().transparent);
    EXPECT_FALSE(ui.params().depth_test);
    ASSERT_EQ(device.pipeline_descriptors.size(), 1u);
    EXPECT_EQ(device.pipeline_descriptors.back().bind_group_layouts.size(), 1u);
    EXPECT_FALSE(device.pipeline_descriptors.back().depth.test_enabled);
    EXPECT_TRUE(device.pipeline_descriptors.back().blend.enabled);
}
