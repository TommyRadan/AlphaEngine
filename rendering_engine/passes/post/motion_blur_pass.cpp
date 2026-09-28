// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/motion_blur_pass.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

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
    // The blur is shaders/passes/motion_blur.frag.glsl, drawn over the
    // shared fullscreen triangle. std140 lays its params block out as two
    // vec4s: blur {shutter scale, max radius in pixels, tap count, noise
    // frame offset} and frame {width, height, 1/width, 1/height}.
    constexpr std::size_t params_ubo_size = 8 * sizeof(float);

    // Tap-count bounds; the shader clamps to the same range.
    constexpr int min_taps = 2;
    constexpr int max_taps = 32;

    // The interleaved-gradient-noise pattern's per-frame offset repeats
    // after this many frames, as in the volumetric fog march.
    constexpr uint64_t noise_frame_period = 64;
} // namespace

namespace rendering_engine
{
    motion_blur_pass::motion_blur_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_fragment_shader =
            gpu::create_library_shader_module(gpu, "passes/motion_blur.frag.glsl", gpu::shader_stage::fragment);

        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        gpu::bind_group_layout_descriptor layout{};
        layout.entries.push_back({0, gpu::binding_kind::texture});
        layout.entries.push_back({1, gpu::binding_kind::texture});
        layout.entries.push_back({2, gpu::binding_kind::uniform_buffer});
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

        // A view's output target, params UBO and bind group wait for
        // prepare(): motion blur is off by default, and a full-resolution
        // target it never draws into is not worth holding. The bind group
        // samples the scene colour and the motion vectors, which are looked
        // up in the view's store; prepare() builds it on the view's first
        // drawn frame and rebuilds it whenever either changes.
    }

    motion_blur_pass::~motion_blur_pass()
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

    motion_blur_pass::view_data::view_data(gpu::device& device) : device{&device}
    {
        // Rewritten every drawn frame (the noise offset moves while
        // temporal AA runs), so host-visible: the device keeps one copy
        // per frame in flight.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = params_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        params_ubo = device.create_buffer(ubo_descriptor);
    }

    motion_blur_pass::view_data::~view_data()
    {
        if (bind_group.valid())
        {
            device->destroy(bind_group);
        }
        // The target owns its colour attachment, so destroying it releases
        // the texture too.
        if (target.valid())
        {
            device->destroy(target);
        }
        if (params_ubo.valid())
        {
            device->destroy(params_ubo);
        }
    }

    void motion_blur_pass::view_data::resize(uint32_t new_width, uint32_t new_height)
    {
        // Same format as the scene colour it stands in for; no depth, the
        // post chain runs depth-disabled.
        gpu::render_target_descriptor descriptor{};
        descriptor.color = {{gpu::texture_format::rgba16_float}};
        descriptor.width = new_width;
        descriptor.height = new_height;
        descriptor.with_depth = false;

        // The replacement first, so the scene colour handle the pass
        // publishes changes and its consumers rebind; the device defers
        // freeing the old target until the last command buffer that used
        // it has retired.
        const gpu::render_target old_target = target;
        target = device->create_render_target(descriptor);
        texture = device->render_target_color_texture(target);
        if (old_target.valid())
        {
            device->destroy(old_target);
        }
        width = new_width;
        height = new_height;
    }

    void motion_blur_pass::rebuild_bind_group(view_data& view, gpu::texture scene_color, gpu::texture velocity)
    {
        auto& gpu = *m_device;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old group has retired.
        if (view.bind_group.valid())
        {
            gpu.destroy(view.bind_group);
            view.bind_group = {};
        }

        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_layout;

        gpu::binding_value color_slot{};
        color_slot.binding = 0;
        color_slot.kind = gpu::binding_kind::texture;
        color_slot.texture_value = scene_color;
        descriptor.entries.push_back(color_slot);

        gpu::binding_value velocity_slot{};
        velocity_slot.binding = 1;
        velocity_slot.kind = gpu::binding_kind::texture;
        velocity_slot.texture_value = velocity;
        descriptor.entries.push_back(velocity_slot);

        gpu::binding_value params_slot{};
        params_slot.binding = 2;
        params_slot.kind = gpu::binding_kind::uniform_buffer;
        params_slot.buffer_value = view.params_ubo;
        descriptor.entries.push_back(params_slot);

        view.bind_group = gpu.create_bind_group(descriptor);
        view.bound_color = scene_color;
        view.bound_velocity = velocity;
    }

    void motion_blur_pass::upload_params(const frame_context& ctx, const view_data& view)
    {
        auto& gpu = *m_device;
        const motion_blur_settings& settings = ctx.post.motion_blur;

        // The noise only moves while temporal AA is there to average it;
        // without it a moving pattern would crawl.
        const float noise_frame =
            ctx.post.taa.enabled ? static_cast<float>(ctx.frame_index % noise_frame_period) : 0.0f;
        const auto width = static_cast<float>(view.width);
        const auto height = static_cast<float>(view.height);
        const std::array<float, 8> params = {
            settings.intensity,
            settings.max_radius,
            static_cast<float>(std::clamp(settings.samples, min_taps, max_taps)),
            noise_frame,
            width,
            height,
            1.0f / width,
            1.0f / height,
        };
        gpu.write_buffer(view.params_ubo, params.data(), params_ubo_size, 0);
    }

    void motion_blur_pass::prepare(const frame_context& ctx)
    {
        // Without motion vectors there is nothing to blur along: publish
        // nothing, so the chain after this pass reads the scene colour.
        const color_target scene_color = ctx.resources->get(frame_resources::scene_color);
        const gpu::texture velocity = ctx.resources->get(frame_resources::velocity);
        m_draws = motion_blur_active(ctx.post.motion_blur) && velocity.valid();
        if (!m_draws)
        {
            return;
        }

        // The view's output target is allocated the first time motion blur
        // is switched on for it, so the default configuration never pays
        // for it, and follows the view's size.
        view_data& view = ctx.view->state<view_data>(*this, *m_device);
        if (view.width != ctx.viewport_width || view.height != ctx.viewport_height)
        {
            view.resize(ctx.viewport_width, ctx.viewport_height);
        }

        // Written here, after begin_frame waited for the frame that last
        // read this frame slot's copy of the buffer.
        upload_params(ctx, view);

        // Bind this frame's scene colour and motion vectors. Both handles
        // are stable until a resize recreates their targets, so compare
        // against what the group was built with and rebuild on change —
        // the first drawn frame included.
        if (scene_color.texture != view.bound_color || velocity != view.bound_velocity || !view.bind_group.valid())
        {
            rebuild_bind_group(view, scene_color.texture, velocity);
        }
        m_target = view.target;
        m_bind_group = view.bind_group;

        // The passes after this one work on the blurred copy.
        ctx.resources->publish(frame_resources::scene_color, color_target{view.target, view.texture});
    }

    void motion_blur_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        if (!m_draws)
        {
            return;
        }

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        // The fullscreen triangle writes every pixel; clearing is strictly
        // redundant but cheap, as in the other fullscreen post passes.
        descriptor.color[0].load = gpu::load_op::clear;
        descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3);
        pass_encoder->end();
    }
} // namespace rendering_engine
