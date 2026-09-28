// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/auto_exposure_pass.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
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
    // The stages are shaders/passes/exposure_{luminance,downsample,adapt,
    // store}.frag.glsl, drawn over the shared fullscreen triangle.

    // Edge of the first (luminance) level; each reduction step divides it
    // by four, down to the 4 x 4 level the adaptation stage averages with
    // four bilinear taps. Baked into the luminance shader as a define.
    constexpr uint32_t luminance_size = 64;

    // std140 lays the adaptation params block out as two vec4s: range
    // {min EV100, max EV100, compensation, reset} and speed {rate up, rate
    // down, frame delta, unused}.
    constexpr std::size_t params_ubo_size = 8 * sizeof(float);
} // namespace

namespace rendering_engine
{
    auto_exposure_pass::auto_exposure_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        // -- Shaders --------------------------------------------------
        const auto fragment_module = [&gpu](const char* path, const gpu::shader_defines& defines) {
            return gpu::create_library_shader_module(
                gpu, gpu::shader_variant{path, defines}, gpu::shader_stage::fragment);
        };

        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);

        gpu::shader_defines luminance_defines;
        luminance_defines.emplace_back("LUMINANCE_SIZE", std::to_string(luminance_size));
        m_luminance_shader = fragment_module("passes/exposure_luminance.frag.glsl", luminance_defines);
        m_downsample_shader = fragment_module("passes/exposure_downsample.frag.glsl", {});
        m_adapt_shader = fragment_module("passes/exposure_adapt.frag.glsl", {});
        m_store_shader = fragment_module("passes/exposure_store.frag.glsl", {});

        // -- Buffers --------------------------------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Layouts --------------------------------------------------
        gpu::bind_group_layout_descriptor texture_layout{};
        texture_layout.entries.push_back({0, gpu::binding_kind::texture});
        m_texture_layout = gpu.create_bind_group_layout(texture_layout);

        gpu::bind_group_layout_descriptor adapt_layout{};
        adapt_layout.entries.push_back({0, gpu::binding_kind::texture});
        adapt_layout.entries.push_back({1, gpu::binding_kind::texture});
        adapt_layout.entries.push_back({2, gpu::binding_kind::uniform_buffer});
        m_adapt_layout = gpu.create_bind_group_layout(adapt_layout);

        // -- Pipelines ------------------------------------------------
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

        const auto make_pipeline = [&](gpu::shader_module fragment, gpu::bind_group_layout layout)
        {
            gpu::pipeline_descriptor descriptor{};
            descriptor.vertex_shader = m_vertex_shader;
            descriptor.fragment_shader = fragment;
            descriptor.vertex_buffers.push_back(vertex_layout);
            descriptor.depth = depth;
            descriptor.blend = blend;
            descriptor.rasterizer = rasterizer;
            descriptor.bind_group_layouts.push_back(layout);
            return gpu.create_pipeline(descriptor);
        };
        m_luminance_pipeline = make_pipeline(m_luminance_shader, m_texture_layout);
        m_downsample_pipeline = make_pipeline(m_downsample_shader, m_texture_layout);
        m_adapt_pipeline = make_pipeline(m_adapt_shader, m_adapt_layout);
        m_store_pipeline = make_pipeline(m_store_shader, m_texture_layout);

        // Each view's targets and the fixed bind groups between them are
        // built by prepare() the first time auto exposure runs for it.
    }

    auto_exposure_pass::~auto_exposure_pass()
    {
        auto& gpu = *m_device;

        // The pipelines, then the layouts, buffers and shaders.
        for (gpu::pipeline* pipeline :
             {&m_store_pipeline, &m_adapt_pipeline, &m_downsample_pipeline, &m_luminance_pipeline})
        {
            if (pipeline->valid())
            {
                gpu.destroy(*pipeline);
                *pipeline = {};
            }
        }
        for (gpu::bind_group_layout* layout : {&m_adapt_layout, &m_texture_layout})
        {
            if (layout->valid())
            {
                gpu.destroy(*layout);
                *layout = {};
            }
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        for (gpu::shader_module* shader :
             {&m_store_shader, &m_adapt_shader, &m_downsample_shader, &m_luminance_shader, &m_vertex_shader})
        {
            if (shader->valid())
            {
                gpu.destroy(*shader);
                *shader = {};
            }
        }
    }

    auto_exposure_pass::view_data::~view_data()
    {
        auto& gpu = *device;

        // Bind groups first, then the targets and buffers they reference.
        const auto destroy_group = [&gpu](gpu::bind_group& group)
        {
            if (group.valid())
            {
                gpu.destroy(group);
                group = {};
            }
        };
        const auto destroy_target = [&gpu](gpu::render_target& target, gpu::texture& texture)
        {
            // The target owns its colour attachment, so this releases the
            // texture too.
            if (target.valid())
            {
                gpu.destroy(target);
                target = {};
                texture = {};
            }
        };

        destroy_group(store_bind_group);
        destroy_group(adapt_bind_group);
        for (auto& level : levels)
        {
            destroy_group(level.source_bind_group);
        }
        destroy_target(history_target, history_texture);
        destroy_target(adapted_target, adapted_texture);
        for (auto& level : levels)
        {
            destroy_target(level.target, level.texture);
        }
        if (params_ubo.valid())
        {
            gpu.destroy(params_ubo);
        }
    }

    void auto_exposure_pass::build_view(view_data& view) const
    {
        auto& gpu = *m_device;

        // Rewritten every metered frame (the frame delta changes), so
        // host-visible: the device keeps one copy per frame in flight.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = params_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        view.params_ubo = gpu.create_buffer(ubo_descriptor);

        // Every target is a fixed size, so everything but level 0's input
        // group (which samples the HDR image the view's store hands over)
        // is built once, here.
        uint32_t size = luminance_size;
        for (size_t i = 0; i < view.levels.size(); ++i)
        {
            view.levels[i].target = create_target(size);
            view.levels[i].texture = gpu.render_target_color_texture(view.levels[i].target);
            if (i > 0)
            {
                view.levels[i].source_bind_group = create_texture_bind_group(view.levels[i - 1].texture);
            }
            size /= 4;
        }

        view.adapted_target = create_target(1);
        view.adapted_texture = gpu.render_target_color_texture(view.adapted_target);
        view.history_target = create_target(1);
        view.history_texture = gpu.render_target_color_texture(view.history_target);

        gpu::bind_group_descriptor adapt_group{};
        adapt_group.layout = m_adapt_layout;
        gpu::binding_value luminance_slot{};
        luminance_slot.binding = 0;
        luminance_slot.kind = gpu::binding_kind::texture;
        luminance_slot.texture_value = view.levels.back().texture;
        adapt_group.entries.push_back(luminance_slot);
        gpu::binding_value history_slot{};
        history_slot.binding = 1;
        history_slot.kind = gpu::binding_kind::texture;
        history_slot.texture_value = view.history_texture;
        adapt_group.entries.push_back(history_slot);
        gpu::binding_value params_slot{};
        params_slot.binding = 2;
        params_slot.kind = gpu::binding_kind::uniform_buffer;
        params_slot.buffer_value = view.params_ubo;
        adapt_group.entries.push_back(params_slot);
        view.adapt_bind_group = gpu.create_bind_group(adapt_group);

        view.store_bind_group = create_texture_bind_group(view.adapted_texture);
    }

    gpu::render_target auto_exposure_pass::create_target(uint32_t size) const
    {
        auto& gpu = *m_device;
        gpu::render_target_descriptor descriptor{};
        descriptor.color = {{gpu::texture_format::rgba16_float}};
        descriptor.width = size;
        descriptor.height = size;
        descriptor.with_depth = false;
        return gpu.create_render_target(descriptor);
    }

    gpu::bind_group auto_exposure_pass::create_texture_bind_group(gpu::texture texture) const
    {
        auto& gpu = *m_device;
        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_texture_layout;
        gpu::binding_value slot{};
        slot.binding = 0;
        slot.kind = gpu::binding_kind::texture;
        slot.texture_value = texture;
        descriptor.entries.push_back(slot);
        return gpu.create_bind_group(descriptor);
    }

    void auto_exposure_pass::upload_params(const frame_context& ctx, const view_data& view, bool reset)
    {
        auto& gpu = *view.device;
        const auto_exposure_settings& settings = ctx.post.auto_exposure;

        // A reversed range is read as the range it spans rather than
        // handed to the shader's clamp, whose result would be undefined.
        const std::array<float, 8> params = {
            std::min(settings.min_ev, settings.max_ev),
            std::max(settings.min_ev, settings.max_ev),
            settings.compensation,
            reset ? 1.0f : 0.0f,
            std::max(settings.speed_up, 0.0f),
            std::max(settings.speed_down, 0.0f),
            std::max(ctx.delta_seconds, 0.0f),
            0.0f,
        };
        gpu.write_buffer(view.params_ubo, params.data(), params_ubo_size, 0);
    }

    void auto_exposure_pass::prepare(const frame_context& ctx)
    {
        m_meters = false;

        // Disabled: record nothing (tonemap uses the manual exposure) and
        // forget the history, so re-enabling snaps to the scene as it is
        // then rather than easing in from whatever it last saw.
        if (!ctx.post.auto_exposure.enabled)
        {
            if (view_data* view = ctx.view->find_state<view_data>(*this))
            {
                view->has_history = false;
            }
            return;
        }

        view_data& view = ctx.view->state<view_data>(*this, *m_device);
        if (!view.adapted_target.valid())
        {
            build_view(view);
        }

        // No camera: the scene pass cleared the HDR image to black, which
        // says nothing about the scene's brightness. Keep the last adapted
        // value, which tonemap keeps sampling once there is one.
        if (ctx.active_camera == nullptr)
        {
            if (view.has_history)
            {
                ctx.resources->publish(frame_resources::exposure, view.adapted_texture);
            }
            return;
        }

        // Written here, after begin_frame waited for the frame that last
        // read this frame slot's copy of the buffer.
        upload_params(ctx, view, !view.has_history);

        // Meter the HDR image tonemap will map this frame. Its handle only
        // changes when motion blur is toggled or a resize recreates the
        // target, so compare against the one level 0's group was built
        // with and rebuild on change — the first metered frame included.
        const gpu::texture hdr = ctx.resources->get(frame_resources::scene_color).texture;
        reduction_level& first = view.levels.front();
        if (hdr != view.bound_input || !first.source_bind_group.valid())
        {
            // Safe mid-frame: the device defers the destroy until the
            // command buffer that may still reference the group retired.
            if (first.source_bind_group.valid())
            {
                m_device->destroy(first.source_bind_group);
            }
            first.source_bind_group = create_texture_bind_group(hdr);
            view.bound_input = hdr;
        }

        // record() meters this frame, so the history it leaves is real and
        // tonemap takes its exposure from it.
        m_meters = true;
        view.has_history = true;
        ctx.resources->publish(frame_resources::exposure, view.adapted_texture);
    }

    void auto_exposure_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        const view_data* view = m_meters ? ctx.view->find_state<view_data>(*this) : nullptr;
        if (view == nullptr)
        {
            return;
        }
        const reduction_level& first = view->levels.front();

        // Draws a fullscreen triangle into @p target; begin_render_pass
        // defaults the viewport to the target's own extent.
        const auto draw_stage = [&](gpu::render_target target, gpu::pipeline pipeline, gpu::bind_group group)
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = target;
            descriptor.color[0].load = gpu::load_op::clear;
            descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(pipeline);
            pass_encoder->set_bind_group(0, group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3);
            pass_encoder->end();
        };

        // 1-2. Log2 luminance, then the reduction down to 4 x 4.
        draw_stage(first.target, m_luminance_pipeline, first.source_bind_group);
        for (size_t i = 1; i < view->levels.size(); ++i)
        {
            draw_stage(view->levels[i].target, m_downsample_pipeline, view->levels[i].source_bind_group);
        }

        // 3. Adapt toward this frame's metered brightness (or snap to it).
        draw_stage(view->adapted_target, m_adapt_pipeline, view->adapt_bind_group);

        // 4. Keep the result as next frame's history.
        draw_stage(view->history_target, m_store_pipeline, view->store_bind_group);
    }
} // namespace rendering_engine
