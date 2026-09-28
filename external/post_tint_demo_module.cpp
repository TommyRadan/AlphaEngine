// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include "api/game_module.hpp"

#include <assets/color.hpp>
#include <assets/mesh_generators.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/lighting/ambient_light.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/pass_list.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/components/mesh_component.hpp>
#include <runtime/engine.hpp>

#include <array>
#include <memory>
#include <utility>

// A lit sphere over a ground plane, seen through a warm colour tint: a
// full-screen post pass this module defines and registers with the
// renderer from outside it, placed right before the built-in tonemap
// pass, so it multiplies the HDR image. While the showcase is enabled it
// also switches the built-in bloom pass off, and it undoes both when it is
// disabled. Nothing in the renderer knows the pass: it reads and writes
// the frame's HDR scene colour through the frame's resource store, under
// the name it declares.

namespace
{
    namespace gpu = rendering_engine::gpu;

    // The name the tint pass registers under.
    constexpr const char* tint_pass_name = "tint";

    // Linear multiplier applied to every HDR texel: a warm, sepia-leaning
    // wash that keeps highlights bright.
    constexpr core::math::vec3 tint_color{1.0f, 0.82f, 0.62f};

    // Multiplies the HDR scene colour by a constant colour, in place: one
    // fullscreen triangle emits the colour and the pipeline blends it as
    // (zero, src_color), so the target's own texels are scaled without
    // being sampled.
    struct tint_pass final : rendering_engine::pass
    {
        tint_pass(gpu::device& device, const core::math::vec3& color) : m_device(&device)
        {
            m_vertex_shader =
                gpu::create_library_shader_module(device, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
            m_fragment_shader =
                gpu::create_library_shader_module(device, "passes/tint.frag.glsl", gpu::shader_stage::fragment);

            gpu::buffer_descriptor vertices{};
            vertices.size = rendering_engine::fullscreen_triangle_vertices.size() * sizeof(float);
            vertices.usage = gpu::buffer_usage_vertex;
            vertices.hint = gpu::buffer_usage_hint::static_data;
            vertices.initial_data = rendering_engine::fullscreen_triangle_vertices.data();
            m_vertex_buffer = device.create_buffer(vertices);

            // std140 vec4: the tint in rgb.
            const std::array<float, 4> block = {color.x, color.y, color.z, 0.0f};
            gpu::buffer_descriptor ubo{};
            ubo.size = block.size() * sizeof(float);
            ubo.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            ubo.hint = gpu::buffer_usage_hint::static_data;
            ubo.initial_data = block.data();
            m_tint_ubo = device.create_buffer(ubo);

            gpu::bind_group_layout_descriptor layout{};
            layout.entries.push_back({0, gpu::binding_kind::uniform_buffer});
            m_layout = device.create_bind_group_layout(layout);

            gpu::bind_group_descriptor group{};
            group.layout = m_layout;
            gpu::binding_value tint_slot{};
            tint_slot.binding = 0;
            tint_slot.kind = gpu::binding_kind::uniform_buffer;
            tint_slot.buffer_value = m_tint_ubo;
            group.entries.push_back(tint_slot);
            m_bind_group = device.create_bind_group(group);

            gpu::vertex_buffer_layout vertex_layout{};
            vertex_layout.stride = sizeof(float) * 2;
            vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

            gpu::pipeline_descriptor pipeline{};
            pipeline.vertex_shader = m_vertex_shader;
            pipeline.fragment_shader = m_fragment_shader;
            pipeline.vertex_buffers.push_back(vertex_layout);
            pipeline.depth.test_enabled = false;
            pipeline.depth.write_enabled = false;
            pipeline.depth.compare = gpu::compare_function::always;
            pipeline.blend.enabled = true;
            pipeline.blend.src = gpu::blend_factor::zero;
            pipeline.blend.dst = gpu::blend_factor::src_color;
            pipeline.blend.op = gpu::blend_op::add;
            pipeline.blend.write_mask = gpu::color_write_red | gpu::color_write_green | gpu::color_write_blue;
            pipeline.rasterizer.cull = gpu::cull_mode::none;
            pipeline.bind_group_layouts.push_back(m_layout);
            m_pipeline = device.create_pipeline(pipeline);
        }

        ~tint_pass() override
        {
            m_device->destroy(m_pipeline);
            m_device->destroy(m_bind_group);
            m_device->destroy(m_layout);
            m_device->destroy(m_tint_ubo);
            m_device->destroy(m_vertex_buffer);
            m_device->destroy(m_fragment_shader);
            m_device->destroy(m_vertex_shader);
        }

        tint_pass(const tint_pass&) = delete;
        tint_pass& operator=(const tint_pass&) = delete;

        // The HDR image as the passes before this one left it: the scene
        // colour, or the copy a pass such as motion blur republished.
        void prepare(const rendering_engine::frame_context& ctx) override
        {
            m_target = ctx.resources->get(rendering_engine::frame_resources::scene_color).target;
        }

        void record(gpu::command_encoder& encoder, const rendering_engine::frame_context& /*ctx*/) override
        {
            if (!m_target.valid())
            {
                return;
            }
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_target;
            descriptor.color[0].load = gpu::load_op::load;
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_pipeline);
            pass_encoder->set_bind_group(0, m_bind_group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3);
            pass_encoder->end();
        }

        const char* name() const override
        {
            return tint_pass_name;
        }

        // Reads and writes the scene colour in place, like bloom: the
        // pass list checks that something before it produced it.
        void declare_io(rendering_engine::pass_io_builder& io) const override
        {
            io.read(rendering_engine::frame_resources::scene_color);
            io.write(rendering_engine::frame_resources::scene_color);
        }

    private:
        gpu::device* m_device{nullptr};
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_tint_ubo{};
        gpu::bind_group_layout m_layout{};
        gpu::bind_group m_bind_group{};
        gpu::pipeline m_pipeline{};

        // The HDR target this frame tints, looked up by prepare().
        gpu::render_target m_target{};
    };

    // Holds the tint pass in the renderer's pass list, before tonemap, and
    // the built-in bloom pass off, while its node is enabled; restores the
    // default chain when the node is disabled or destroyed.
    struct post_tint final : runtime::behavior
    {
        void on_enable() override
        {
            rendering_engine::renderer& renderer = *runtime::current_engine().renderer;
            renderer.add_pass(std::make_unique<tint_pass>(renderer.device(), tint_color),
                              rendering_engine::pass_placement::before(rendering_engine::builtin_passes::tonemap));
            renderer.set_pass_enabled(rendering_engine::builtin_passes::bloom, false);
        }

        void on_disable() override
        {
            rendering_engine::renderer& renderer = *runtime::current_engine().renderer;
            renderer.remove_pass(tint_pass_name);
            renderer.set_pass_enabled(rendering_engine::builtin_passes::bloom, true);
        }
    };

    // A new phong instance with the showcase's blue diffuse and white
    // highlight, for one object to own through its mesh component.
    std::shared_ptr<rendering_engine::phong_material> make_material()
    {
        std::shared_ptr<rendering_engine::phong_material> material =
            runtime::current_engine().renderer->materials().create_material<rendering_engine::phong_material>("phong");
        material->set_diffuse(assets::color{120, 170, 230, 255});
        material->set_specular(assets::color{255, 255, 255, 255});
        material->set_shininess(64.0f);
        return material;
    }

    // A child of @p parent carrying the light @p light.
    runtime::node&
    spawn_light(runtime::scene& scene, runtime::node& parent, std::unique_ptr<rendering_engine::light> light)
    {
        runtime::node& holder = scene.create_node({}, &parent);
        holder.add_component(runtime::light_component{std::move(light)});
        return holder;
    }
} // namespace

GAME_MODULE()
{
    auto& cache = *runtime::current_engine().assets;

    runtime::node& demo = scene.create_node("post_tint_demo");
    runtime::add_behavior<post_tint>(demo);

    auto ball = cache.get_or_create_mesh(assets::mesh_generators::sphere{});
    runtime::node& sphere = scene.create_node("sphere", &demo);
    sphere.add_component(runtime::mesh_component{make_material(), std::move(ball)});

    auto plane = cache.get_or_create_mesh(assets::mesh_generators::plane{.width = 30.0f, .height = 30.0f});
    runtime::node& ground = scene.create_node("ground", &demo);
    ground.transform.set_position(core::math::vec3{0.0f, 0.0f, -1.5f});
    ground.add_component(runtime::mesh_component{make_material(), std::move(plane)});

    auto ambient = std::make_unique<rendering_engine::ambient_light>();
    ambient->color = core::math::vec3{1.0f, 1.0f, 1.0f};
    ambient->intensity = 0.15f;
    spawn_light(scene, demo, std::move(ambient));

    // The camera looks from -X toward the origin; a sun travelling +X and
    // down lights the camera-facing side and casts onto the ground.
    auto sun_light = std::make_unique<rendering_engine::directional_light>();
    sun_light->color = core::math::vec3{1.0f, 1.0f, 1.0f};
    sun_light->intensity = 1.2f;
    sun_light->cast_shadow = true;
    runtime::node& sun = spawn_light(scene, demo, std::move(sun_light));
    sun.look_at(sun.world_position() + core::math::vec3{1.0f, -0.4f, -0.6f});
}
