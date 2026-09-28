// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/skybox_pass.hpp>

#include <string>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>
#include <rendering_engine/passes/projection_jitter.hpp>
#include <rendering_engine/render_world.hpp>

namespace
{
    // The sky is shaders/passes/skybox.{vert,frag}.glsl drawn over the
    // shared fullscreen triangle, pinned to the far plane so the depth
    // test rejects it wherever scene geometry already wrote a nearer depth.

    // std140 size of the Skybox UBO: a single mat4.
    constexpr size_t sky_ubo_size = sizeof(core::math::mat4);
} // namespace

namespace rendering_engine
{
    skybox_pass::skybox_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        m_vertex_shader = gpu::create_library_shader_module(gpu, "passes/skybox.vert.glsl", gpu::shader_stage::vertex);
        m_fragment_shader =
            gpu::create_library_shader_module(gpu, "passes/skybox.frag.glsl", gpu::shader_stage::fragment);

        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::uniform_buffer});
        input_layout.entries.push_back({1, gpu::binding_kind::texture});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        // Sky behind everything: depth tested against the loaded scene
        // depth with less-or-equal so the far-plane fragments pass where
        // the scene left the cleared 1.0, but never write depth. No
        // culling — the oversized triangle's winding is irrelevant.
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = true;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::less_equal;

        gpu::blend_state blend{};
        blend.enabled = false;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::none;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = gpu::polygon_mode::fill;

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.fragment_shader = m_fragment_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.bind_group_layouts.push_back(m_input_layout);

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    skybox_pass::~skybox_pass()
    {
        auto& gpu = *m_device;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_input_layout.valid())
        {
            gpu.destroy(m_input_layout);
            m_input_layout = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_fragment_shader.valid())
        {
            gpu.destroy(m_fragment_shader);
            m_fragment_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    skybox_pass::view_data::view_data(gpu::device& device) : device{&device}
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sky_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        sky_ubo = device.create_buffer(ubo_descriptor);
    }

    skybox_pass::view_data::~view_data()
    {
        if (input_bind_group.valid())
        {
            device->destroy(input_bind_group);
        }
        if (sky_ubo.valid())
        {
            device->destroy(sky_ubo);
        }
    }

    void skybox_pass::rebuild_bind_group(view_data& view, gpu::texture cubemap)
    {
        auto& gpu = *m_device;
        if (view.input_bind_group.valid())
        {
            gpu.destroy(view.input_bind_group);
            view.input_bind_group = {};
        }

        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_input_layout;

        gpu::binding_value sky_slot{};
        sky_slot.binding = 0;
        sky_slot.kind = gpu::binding_kind::uniform_buffer;
        sky_slot.buffer_value = view.sky_ubo;
        descriptor.entries.push_back(sky_slot);

        gpu::binding_value cube_slot{};
        cube_slot.binding = 1;
        cube_slot.kind = gpu::binding_kind::texture;
        cube_slot.texture_value = cubemap;
        descriptor.entries.push_back(cube_slot);

        view.input_bind_group = gpu.create_bind_group(descriptor);
        view.bound_cubemap = cubemap;
    }

    void skybox_pass::prepare(const frame_context& ctx)
    {
        m_target = ctx.resources->get(frame_resources::scene_color).target;

        // Dormant without a cube map, and a no-camera frame has no view ray
        // to reconstruct — leave the scene colour untouched.
        const environment_probe* environment = ctx.world != nullptr ? ctx.world->environment() : nullptr;
        const gpu::texture cubemap = environment != nullptr ? environment->skybox() : gpu::texture{};
        m_draws = cubemap.valid() && ctx.active_camera != nullptr;
        if (!m_draws)
        {
            return;
        }

        // Follow the world's environment: a new probe swaps the cube map
        // the view's input bind group samples.
        view_data& state = ctx.view->state<view_data>(*this, *m_device);
        if (cubemap != state.bound_cubemap || !state.input_bind_group.valid())
        {
            rebuild_bind_group(state, cubemap);
        }
        m_input_bind_group = state.input_bind_group;

        auto& gpu = *m_device;
        // Strip the translation from the view matrix so the sky rotates
        // with the camera but never translates, then invert
        // projection * view so the vertex shader can unproject screen
        // corners into world-space ray directions. The projection carries
        // the frame's temporal-AA jitter, exactly as the scene pass
        // rasterised the depth this sky is tested against: the sky is then
        // sampled a sub-pixel apart each frame like the geometry, so the
        // TAA accumulation supersamples it too and silhouettes against it
        // line up. A zero jitter (TAA off) leaves the matrix untouched.
        core::math::mat4 view = ctx.active_camera->view;
        view.data()[12] = 0.0f;
        view.data()[13] = 0.0f;
        view.data()[14] = 0.0f;
        const core::math::mat4 projection = jitter_projection(ctx.active_camera->projection, ctx.jitter);
        const core::math::mat4 inv_view_proj = core::math::inverse(projection * view);
        gpu.write_buffer(state.sky_ubo, inv_view_proj.data(), sky_ubo_size, 0);
    }

    void skybox_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        if (!m_draws)
        {
            return;
        }

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        // Load the scene pass's colour and depth: the sky composites behind
        // the geometry it already drew rather than wiping it.
        descriptor.color[0].load = gpu::load_op::load;
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::load;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3);
        pass_encoder->end();
    }
} // namespace rendering_engine
