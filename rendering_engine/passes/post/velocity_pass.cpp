// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/velocity_pass.hpp>

#include <array>
#include <cstring>
#include <string>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>

namespace
{
    // The reprojection is shaders/passes/velocity.frag.glsl, drawn over the
    // shared fullscreen triangle: each pixel's previous-frame UV is
    // reconstructed from the scene depth through the baked
    // prevViewProj * inverse(curViewProj) matrix, after the frame's
    // temporal-AA jitter is subtracted from its NDC position, and the
    // motion vector is the UV delta the TAA resolve reads history at. The
    // depth helpers it includes live in shaders/include/depth_utils.glsl.

    // std140 layout of the Reprojection block: mat4 reprojection at
    // offset 0, vec4 jitter (xy = this frame's NDC jitter) at offset 64.
    constexpr size_t reproj_ubo_size = sizeof(core::math::mat4) + 4 * sizeof(float);
} // namespace

namespace rendering_engine
{
    velocity_pass::velocity_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_fragment_shader =
            gpu::create_library_shader_module(gpu, "passes/velocity.frag.glsl", gpu::shader_stage::fragment);

        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        gpu::bind_group_layout_descriptor layout{};
        layout.entries.push_back({0, gpu::binding_kind::texture});
        layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_layout = gpu.create_bind_group_layout(layout);

        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = false;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::always;

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
        pipeline_descriptor.bind_group_layouts.push_back(m_layout);
        m_pipeline = gpu.create_pipeline(pipeline_descriptor);

        // Each view's target, reprojection block and input bind group are
        // built by prepare(): the scene depth the group samples is looked
        // up in the view's store and rebound whenever that handle changes.
    }

    velocity_pass::~velocity_pass()
    {
        auto& gpu = *m_device;

        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_layout.valid())
        {
            gpu.destroy(m_layout);
            m_layout = {};
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

    velocity_pass::view_data::view_data(gpu::device& device) : device{&device}
    {
        // The reprojection matrix is rewritten every frame from the live
        // camera, so the buffer is dynamic and copy-dst.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = reproj_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        reproj_ubo = device.create_buffer(ubo_descriptor);
    }

    velocity_pass::view_data::~view_data()
    {
        if (bind_group.valid())
        {
            device->destroy(bind_group);
        }
        if (velocity_target.valid())
        {
            device->destroy(velocity_target);
        }
        if (reproj_ubo.valid())
        {
            device->destroy(reproj_ubo);
        }
    }

    void velocity_pass::view_data::resize(uint32_t new_width, uint32_t new_height)
    {
        // Two-channel signed motion needs a float target; the engine has no
        // RG format, so rgba16f carries the vector in xy and leaves zw at 0.
        gpu::render_target_descriptor velocity_descriptor{};
        velocity_descriptor.color = {{gpu::texture_format::rgba16_float}};
        velocity_descriptor.width = new_width;
        velocity_descriptor.height = new_height;
        velocity_descriptor.with_depth = false;

        // The replacement first, so the TAA resolve, which compares the
        // published velocity texture against the handle it bound, sees a
        // different handle. The device defers freeing the old target until
        // the last command buffer that sampled it has retired. The input
        // bind group samples the scene depth, not this target, and follows
        // that handle on its own.
        const gpu::render_target old_target = velocity_target;
        velocity_target = device->create_render_target(velocity_descriptor);
        velocity_texture = device->render_target_color_texture(velocity_target);
        if (old_target.valid())
        {
            device->destroy(old_target);
        }
        width = new_width;
        height = new_height;
    }

    void velocity_pass::rebuild_bind_group(view_data& view, gpu::texture scene_depth)
    {
        auto& gpu = *m_device;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old group has retired.
        if (view.bind_group.valid())
        {
            gpu.destroy(view.bind_group);
            view.bind_group = {};
        }

        gpu::bind_group_descriptor bind_group_descriptor{};
        bind_group_descriptor.layout = m_layout;

        gpu::binding_value depth_slot{};
        depth_slot.binding = 0;
        depth_slot.kind = gpu::binding_kind::texture;
        depth_slot.texture_value = scene_depth;
        bind_group_descriptor.entries.push_back(depth_slot);

        gpu::binding_value reproj_slot{};
        reproj_slot.binding = 1;
        reproj_slot.kind = gpu::binding_kind::uniform_buffer;
        reproj_slot.buffer_value = view.reproj_ubo;
        bind_group_descriptor.entries.push_back(reproj_slot);

        view.bind_group = gpu.create_bind_group(bind_group_descriptor);
        view.bound_depth = scene_depth;
    }

    void velocity_pass::prepare(const frame_context& ctx)
    {
        m_action = frame_action::none;

        // The view's target follows the view's size. Published every frame,
        // drawn or not: a consumer only samples it on frames that make this
        // pass draw.
        view_data& state = ctx.view->state<view_data>(*this, *m_device);
        if (state.width != ctx.viewport_width || state.height != ctx.viewport_height)
        {
            state.resize(ctx.viewport_width, ctx.viewport_height);
        }
        m_target = state.velocity_target;
        ctx.resources->publish(frame_resources::velocity, state.velocity_texture);

        // Only the TAA resolve and motion blur read the motion vectors;
        // with neither running this frame there is nothing to write.
        if (!ctx.post.taa.enabled && !motion_blur_active(ctx.post.motion_blur))
        {
            return;
        }

        auto& gpu = *m_device;

        // No camera, or no scene depth to reconstruct positions from: clear
        // the motion to zero so the TAA resolve falls back to same-pixel
        // history. The renderer drops the view's previous view-projection
        // across such a frame, so the next camera frame starts fresh (zero
        // motion) rather than reprojecting across the gap.
        const gpu::texture scene_depth = ctx.resources->get(frame_resources::scene_depth);
        if (ctx.active_camera == nullptr || !scene_depth.valid())
        {
            m_action = frame_action::clear;
            return;
        }

        // Build the current unjittered view-projection (the camera's
        // matrices are clean; the scene pass applies frame_context::jitter
        // on top of them, and the shader subtracts it again).
        const core::math::mat4& view = ctx.active_camera->view;
        const core::math::mat4& projection = ctx.active_camera->projection;
        const core::math::mat4 view_proj = projection * view;

        // Without a usable previous matrix (the view's first frame, the
        // frame after a camera-less one) reproject against this frame so
        // every pixel reports zero motion.
        const core::math::mat4& prev_view_proj = ctx.has_prev_view_projection ? ctx.prev_view_projection : view_proj;
        const core::math::mat4 reprojection = prev_view_proj * core::math::inverse(view_proj);
        std::array<float, 20> reproj_payload{};
        std::memcpy(reproj_payload.data(), reprojection.data(), sizeof(core::math::mat4));
        reproj_payload[16] = ctx.jitter.x;
        reproj_payload[17] = ctx.jitter.y;
        gpu.write_buffer(state.reproj_ubo, reproj_payload.data(), reproj_ubo_size, 0);

        // Bind this frame's scene depth. The handle is stable today, but a
        // resized scene target swaps its attachment, so compare against the
        // one the bind group was built with and rebuild on change (the first
        // camera frame included).
        if (scene_depth != state.bound_depth || !state.bind_group.valid())
        {
            rebuild_bind_group(state, scene_depth);
        }
        m_bind_group = state.bind_group;
        m_action = frame_action::draw;
    }

    void velocity_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        if (m_action == frame_action::none)
        {
            return;
        }

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        if (m_action == frame_action::draw)
        {
            pass_encoder->set_pipeline(m_pipeline);
            pass_encoder->set_bind_group(0, m_bind_group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3);
        }
        pass_encoder->end();
    }
} // namespace rendering_engine
