// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/taa_pass.hpp>

#include <array>
#include <string>

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
    // The resolve is shaders/passes/taa_resolve.frag.glsl, drawn over the
    // shared fullscreen triangle. u_taa.params.xy is (1/width, 1/height),
    // the per-texel step the neighbourhood taps walk by, and params.z is
    // the history feedback weight — baked to 0 while the history is
    // unusable and to frame_context::post.taa.feedback thereafter (see
    // rendering_engine/post_settings.hpp's taa_settings::feedback for what
    // the steady-state weight trades off).

    // std140 rounds the single vec4 block up to a 16-byte allocation.
    constexpr size_t taa_ubo_size = 16;
} // namespace

namespace rendering_engine
{
    taa_pass::taa_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        // -- Shaders --------------------------------------------------
        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_resolve_shader =
            gpu::create_library_shader_module(gpu, "passes/taa_resolve.frag.glsl", gpu::shader_stage::fragment);

        // -- Fullscreen-triangle vertex buffer ------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Bind-group layout ----------------------------------------
        gpu::bind_group_layout_descriptor resolve_layout{};
        resolve_layout.entries.push_back({0, gpu::binding_kind::texture});
        resolve_layout.entries.push_back({1, gpu::binding_kind::texture});
        resolve_layout.entries.push_back({2, gpu::binding_kind::texture});
        resolve_layout.entries.push_back({3, gpu::binding_kind::uniform_buffer});
        m_resolve_layout = gpu.create_bind_group_layout(resolve_layout);

        // -- Pipeline -------------------------------------------------
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
        pipeline_descriptor.fragment_shader = m_resolve_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.bind_group_layouts.push_back(m_resolve_layout);
        m_resolve_pipeline = gpu.create_pipeline(pipeline_descriptor);

        // Each view's pair, params UBO and resolve bind groups are built by
        // prepare(): the groups sample the LDR image and the motion vectors,
        // which are looked up in the view's store, and are rebuilt whenever
        // either handle changes.
    }

    taa_pass::view_data::view_data(gpu::device& device) : device{&device}
    {
        // The feedback starts at 0 so the view's first frame ignores the
        // still undefined history; prepare() writes the texel step with it
        // and bumps it to frame_context::post's taa.feedback once a frame
        // of the view has been resolved.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = taa_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        resolve_ubo = device.create_buffer(ubo_descriptor);
    }

    taa_pass::view_data::~view_data()
    {
        destroy_resolve_bind_groups();
        // Each render target owns its colour attachment, so destroying the
        // target releases the texture too.
        for (auto& half : targets)
        {
            if (half.target.valid())
            {
                device->destroy(half.target);
            }
        }
        if (resolve_ubo.valid())
        {
            device->destroy(resolve_ubo);
        }
    }

    void taa_pass::view_data::resize(uint32_t new_width, uint32_t new_height)
    {
        // Both rgba8, no depth: the post chain runs depth-disabled and the
        // image is already tonemapped LDR at this point. The replacements
        // are created before the old targets are released so the published
        // resolve handle changes and FXAA rebinds; the device retires the
        // attachments only once the last command buffer that sampled them
        // has finished.
        const std::array<gpu::render_target, 2> old_targets = {targets[0].target, targets[1].target};
        for (auto& half : targets)
        {
            gpu::render_target_descriptor descriptor{};
            descriptor.color = {{gpu::texture_format::rgba8_unorm}};
            descriptor.width = new_width;
            descriptor.height = new_height;
            descriptor.with_depth = false;
            half.target = device->create_render_target(descriptor);
            half.texture = device->render_target_color_texture(half.target);
        }
        for (const auto& old : old_targets)
        {
            if (old.valid())
            {
                device->destroy(old);
            }
        }

        // The resolve bind groups sample the replaced targets as history:
        // drop them and forget the inputs they were built with so the next
        // prepare() rebuilds them against the new pair (and whatever LDR /
        // velocity handles that frame publishes).
        destroy_resolve_bind_groups();
        bound_current = {};
        bound_velocity = {};

        // The history is a differently sized image of a differently
        // projected frame, if any: drop it. Pin the feedback to 0 again so
        // the first frame at the new size resolves from the current image
        // alone. The params UBO is not touched here: it is host-mapped on
        // a deferred-execution backend and the previous frame may still be
        // reading it, so prepare() rewrites it (with the new texel step)
        // once begin_frame has waited for that frame.
        width = new_width;
        height = new_height;
        inv_width = 1.0f / static_cast<float>(new_width);
        inv_height = 1.0f / static_cast<float>(new_height);
        first_frame = true;
        uploaded_feedback = -1.0f;
    }

    void taa_pass::view_data::destroy_resolve_bind_groups()
    {
        for (auto& half : targets)
        {
            if (half.resolve_bind_group.valid())
            {
                device->destroy(half.resolve_bind_group);
                half.resolve_bind_group = {};
            }
        }
    }

    void taa_pass::rebuild_resolve_bind_groups(view_data& view, gpu::texture current_color, gpu::texture velocity)
    {
        auto& gpu = *m_device;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old groups has retired.
        view.destroy_resolve_bind_groups();

        for (size_t index = 0; index < view.targets.size(); ++index)
        {
            gpu::bind_group_descriptor resolve_bind_group_descriptor{};
            resolve_bind_group_descriptor.layout = m_resolve_layout;

            gpu::binding_value current_slot{};
            current_slot.binding = 0;
            current_slot.kind = gpu::binding_kind::texture;
            current_slot.texture_value = current_color;
            resolve_bind_group_descriptor.entries.push_back(current_slot);

            // The half being written samples the other half as history.
            gpu::binding_value history_slot{};
            history_slot.binding = 1;
            history_slot.kind = gpu::binding_kind::texture;
            history_slot.texture_value = view.targets[1 - index].texture;
            resolve_bind_group_descriptor.entries.push_back(history_slot);

            gpu::binding_value velocity_slot{};
            velocity_slot.binding = 2;
            velocity_slot.kind = gpu::binding_kind::texture;
            velocity_slot.texture_value = velocity;
            resolve_bind_group_descriptor.entries.push_back(velocity_slot);

            gpu::binding_value params_slot{};
            params_slot.binding = 3;
            params_slot.kind = gpu::binding_kind::uniform_buffer;
            params_slot.buffer_value = view.resolve_ubo;
            resolve_bind_group_descriptor.entries.push_back(params_slot);

            view.targets[index].resolve_bind_group = gpu.create_bind_group(resolve_bind_group_descriptor);
        }
        view.bound_current = current_color;
        view.bound_velocity = velocity;
    }

    void taa_pass::write_params(view_data& view, float feedback)
    {
        // Rewrite the whole vec4 so the texel step (xy) travels with the
        // feedback weight (z).
        const std::array<float, 4> params = {view.inv_width, view.inv_height, feedback, 0.0f};
        view.device->write_buffer(view.resolve_ubo, params.data(), taa_ubo_size, 0);
        view.uploaded_feedback = feedback;
    }

    taa_pass::~taa_pass()
    {
        auto& gpu = *m_device;

        if (m_resolve_pipeline.valid())
        {
            gpu.destroy(m_resolve_pipeline);
            m_resolve_pipeline = {};
        }
        if (m_resolve_layout.valid())
        {
            gpu.destroy(m_resolve_layout);
            m_resolve_layout = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_resolve_shader.valid())
        {
            gpu.destroy(m_resolve_shader);
            m_resolve_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    void taa_pass::prepare(const frame_context& ctx)
    {
        // The view's pair follows the view's size; a new or resized pair
        // holds no history yet.
        view_data& view = ctx.view->state<view_data>(*this, *m_device);
        if (view.width != ctx.viewport_width || view.height != ctx.viewport_height)
        {
            view.resize(ctx.viewport_width, ctx.viewport_height);
        }

        // The history belongs to the view's camera: the view is keyed by
        // it, so a camera switch or a teleport by reattaching starts a new
        // view and a new history. A camera-less frame restarts the
        // accumulation too, so nothing ghosts against a stale image.
        if (ctx.active_camera == nullptr)
        {
            view.first_frame = true;
        }

        // Bind this frame's LDR image and motion vectors. Both handles are
        // stable from frame to frame, but a resize recreates the targets
        // behind them (and resize() forgets the bound pair so the new
        // history is picked up), so compare against what the groups were
        // built with and rebuild on change — the first frame included.
        const accumulation_target& write = view.targets[view.write_index];
        const gpu::texture current = ctx.resources->get(frame_resources::ldr_color).texture;
        const gpu::texture velocity = ctx.resources->get(frame_resources::velocity);
        if (current != view.bound_current || velocity != view.bound_velocity || !write.resolve_bind_group.valid())
        {
            rebuild_resolve_bind_groups(view, current, velocity);
        }
        m_draw_target = write.target;
        m_draw_bind_group = write.resolve_bind_group;
        ctx.resources->publish(frame_resources::taa_resolve, write.texture);

        // While the history is unusable the feedback is pinned to 0
        // (current frame only); every later frame accumulates with the
        // runtime-tunable steady-state weight from frame_context::post.
        // Written here, before the draw and after begin_frame waited for
        // the previous frame, so the value the GPU reads for this frame is
        // the one this frame needs — a mismatch also covers the texel step
        // a resize changed and a live change to the feedback weight.
        const float feedback = view.first_frame ? 0.0f : ctx.post.taa.feedback;
        if (feedback != view.uploaded_feedback)
        {
            write_params(view, feedback);
        }

        // This frame's half will hold a real frame of this view once
        // record() draws it: swap the roles so the view's next frame reads
        // it as history, and switch to the steady-state feedback so
        // subsequent frames accumulate.
        view.write_index = 1u - view.write_index;
        view.first_frame = false;
    }

    void taa_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        // Resolve: blend the current LDR frame with the clamped history
        // (the other half of the pair) into this frame's half, which the
        // next pass (FXAA) samples and the view's next frame reads as
        // history.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_draw_target;
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_resolve_pipeline);
        pass_encoder->set_bind_group(0, m_draw_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3);
        pass_encoder->end();
    }
} // namespace rendering_engine
