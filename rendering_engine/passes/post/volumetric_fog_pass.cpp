// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/volumetric_fog_pass.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>

namespace
{
    // The three stages are shaders/passes/volumetric_fog.frag.glsl (the
    // half-resolution march), volumetric_fog_upsample.frag.glsl (the
    // depth-aware upsample) and volumetric_fog_composite.frag.glsl (the
    // blend over the scene colour), all drawn over the shared fullscreen
    // triangle.

    // std140 layout of the VolumetricFog block the march and the upsample
    // share: vec4 medium (x density scale, y anisotropy, z max distance,
    // w intensity) at 0, vec4 march (x step count, y noise frame offset)
    // at 16, mat4 inverseProjection at 32. 96 bytes, no padding.
    constexpr std::size_t params_floats = 4 + 4 + 16;
    constexpr std::size_t params_ubo_size = params_floats * sizeof(float);

    // The step count's range, matching FOG_MAX_STEPS in the march shader.
    constexpr int min_steps = 1;
    constexpr int max_steps = 128;

    // |g| of the Henyey-Greenstein phase is kept below 1, where the lobe
    // degenerates into a delta.
    constexpr float max_anisotropy = 0.99f;

    // The interleaved-gradient-noise pattern repeats its per-frame offset
    // after this many frames, keeping the float the shader adds exact.
    constexpr uint64_t noise_frame_period = 64;

    // The march target covers the drawable at half resolution, rounded up
    // so every full-resolution pixel has the texel of its 2x2 block.
    uint32_t half_extent(uint32_t full)
    {
        return std::max(1u, (full + 1) / 2);
    }

    // Packs the std140 VolumetricFog block: the settings clamped to what
    // the shaders handle, the noise frame offset, and the inverse
    // projection the upsample linearises depth with.
    std::array<float, params_floats> pack_params(const rendering_engine::volumetric_fog_settings& settings,
                                                 float frame_offset,
                                                 const core::math::mat4& inverse_projection)
    {
        std::array<float, params_floats> params{};
        params[0] = settings.density_scale;
        params[1] = std::clamp(settings.anisotropy, -max_anisotropy, max_anisotropy);
        params[2] = settings.max_distance;
        params[3] = std::max(settings.intensity, 0.0f);
        params[4] = static_cast<float>(std::clamp(settings.steps, min_steps, max_steps));
        params[5] = frame_offset;
        std::memcpy(params.data() + 8, inverse_projection.data(), sizeof(core::math::mat4));
        return params;
    }
} // namespace

namespace rendering_engine
{
    volumetric_fog_pass::volumetric_fog_pass(gpu::device& device, gpu::bind_group_layout frame_layout)
        : m_device(&device)
    {
        auto& gpu = *m_device;

        // -- Shaders --------------------------------------------------
        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_march_shader =
            gpu::create_library_shader_module(gpu, "passes/volumetric_fog.frag.glsl", gpu::shader_stage::fragment);
        m_upsample_shader = gpu::create_library_shader_module(
            gpu, "passes/volumetric_fog_upsample.frag.glsl", gpu::shader_stage::fragment);
        m_composite_shader = gpu::create_library_shader_module(
            gpu, "passes/volumetric_fog_composite.frag.glsl", gpu::shader_stage::fragment);

        // -- Fullscreen-triangle vertex buffer ------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Layouts --------------------------------------------------
        // The march's slot 1 sits beside the scene's per-frame set, so its
        // bindings come from the shared table and stay unique across both
        // sets.
        gpu::bind_group_layout_descriptor march_layout{};
        march_layout.entries.push_back(
            {gpu::shader_bindings::volumetric_fog_params, gpu::binding_kind::uniform_buffer});
        march_layout.entries.push_back({gpu::shader_bindings::volumetric_fog_depth, gpu::binding_kind::texture});
        m_march_layout = gpu.create_bind_group_layout(march_layout);

        gpu::bind_group_layout_descriptor upsample_layout{};
        upsample_layout.entries.push_back({0, gpu::binding_kind::texture});
        upsample_layout.entries.push_back({1, gpu::binding_kind::texture});
        upsample_layout.entries.push_back({2, gpu::binding_kind::uniform_buffer});
        m_upsample_layout = gpu.create_bind_group_layout(upsample_layout);

        gpu::bind_group_layout_descriptor composite_layout{};
        composite_layout.entries.push_back({0, gpu::binding_kind::texture});
        m_composite_layout = gpu.create_bind_group_layout(composite_layout);

        // -- Fixed-function state shared by the three stages -----------
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = false;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::always;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::none;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = gpu::polygon_mode::fill;

        // The march and the upsample overwrite their own targets.
        gpu::blend_state opaque_blend{};
        opaque_blend.enabled = false;

        // The composite: dst = src.rgb + dst.rgb * src.a, i.e. the scene
        // attenuated by the fog's transmittance plus its in-scattered
        // light. Alpha is masked off so the scene target's alpha stays as
        // the scene pass left it.
        gpu::blend_state composite_blend{};
        composite_blend.enabled = true;
        composite_blend.src = gpu::blend_factor::one;
        composite_blend.dst = gpu::blend_factor::src_alpha;
        composite_blend.op = gpu::blend_op::add;
        composite_blend.write_mask = gpu::color_write_red | gpu::color_write_green | gpu::color_write_blue;

        auto make_pipeline = [&](gpu::shader_module fragment,
                                 const gpu::blend_state& blend,
                                 std::initializer_list<gpu::bind_group_layout> layouts)
        {
            gpu::pipeline_descriptor descriptor{};
            descriptor.vertex_shader = m_vertex_shader;
            descriptor.fragment_shader = fragment;
            descriptor.vertex_buffers.push_back(vertex_layout);
            descriptor.depth = depth;
            descriptor.blend = blend;
            descriptor.rasterizer = rasterizer;
            descriptor.bind_group_layouts.assign(layouts.begin(), layouts.end());
            return gpu.create_pipeline(descriptor);
        };

        m_march_pipeline = make_pipeline(m_march_shader, opaque_blend, {frame_layout, m_march_layout});
        m_upsample_pipeline = make_pipeline(m_upsample_shader, opaque_blend, {m_upsample_layout});
        m_composite_pipeline = make_pipeline(m_composite_shader, composite_blend, {m_composite_layout});

        // Each view's targets, params and bind groups are built lazily by
        // prepare(): the march and the upsample sample the scene depth,
        // which is looked up in the view's store and rebound whenever that
        // handle changes.
    }

    volumetric_fog_pass::~volumetric_fog_pass()
    {
        auto& gpu = *m_device;

        // The pipelines, then the layouts, buffers and shaders.
        if (m_composite_pipeline.valid())
        {
            gpu.destroy(m_composite_pipeline);
            m_composite_pipeline = {};
        }
        if (m_upsample_pipeline.valid())
        {
            gpu.destroy(m_upsample_pipeline);
            m_upsample_pipeline = {};
        }
        if (m_march_pipeline.valid())
        {
            gpu.destroy(m_march_pipeline);
            m_march_pipeline = {};
        }
        if (m_composite_layout.valid())
        {
            gpu.destroy(m_composite_layout);
            m_composite_layout = {};
        }
        if (m_upsample_layout.valid())
        {
            gpu.destroy(m_upsample_layout);
            m_upsample_layout = {};
        }
        if (m_march_layout.valid())
        {
            gpu.destroy(m_march_layout);
            m_march_layout = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_composite_shader.valid())
        {
            gpu.destroy(m_composite_shader);
            m_composite_shader = {};
        }
        if (m_upsample_shader.valid())
        {
            gpu.destroy(m_upsample_shader);
            m_upsample_shader = {};
        }
        if (m_march_shader.valid())
        {
            gpu.destroy(m_march_shader);
            m_march_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    volumetric_fog_pass::view_data::view_data(gpu::device& device) : device{&device}
    {
        // The params follow the live settings, frame and camera, so the
        // buffer is rewritten every drawn frame: dynamic and copy-dst.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = params_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        params_ubo = device.create_buffer(ubo_descriptor);
    }

    volumetric_fog_pass::view_data::~view_data()
    {
        // Bind groups first, then the targets and buffers they reference.
        release_bind_groups();
        if (upsample_target.valid())
        {
            device->destroy(upsample_target);
        }
        if (march_target.valid())
        {
            device->destroy(march_target);
        }
        if (params_ubo.valid())
        {
            device->destroy(params_ubo);
        }
    }

    void volumetric_fog_pass::view_data::resize(uint32_t new_width, uint32_t new_height)
    {
        // Every consumer of the two targets is one of this pass's own bind
        // groups, so they go first and the frame rebuilds them against the
        // new targets. The device retires the old attachments only once
        // the last command buffer that used them has finished.
        release_bind_groups();
        const gpu::render_target old_march = march_target;
        const gpu::render_target old_upsample = upsample_target;

        march_target = device->create_render_target(gpu::render_target_descriptor::single_color(
            gpu::texture_format::rgba16_float, half_extent(new_width), half_extent(new_height)));
        march_texture = device->render_target_color_texture(march_target);
        upsample_target = device->create_render_target(
            gpu::render_target_descriptor::single_color(gpu::texture_format::rgba16_float, new_width, new_height));
        upsample_texture = device->render_target_color_texture(upsample_target);

        if (old_upsample.valid())
        {
            device->destroy(old_upsample);
        }
        if (old_march.valid())
        {
            device->destroy(old_march);
        }
        width = new_width;
        height = new_height;
    }

    void volumetric_fog_pass::view_data::release_bind_groups()
    {
        // Safe mid-frame as well as between frames: the device defers each
        // destroy until the command buffer that may still reference the
        // group has retired.
        if (composite_bind_group.valid())
        {
            device->destroy(composite_bind_group);
            composite_bind_group = {};
        }
        if (upsample_bind_group.valid())
        {
            device->destroy(upsample_bind_group);
            upsample_bind_group = {};
        }
        if (march_bind_group.valid())
        {
            device->destroy(march_bind_group);
            march_bind_group = {};
        }
        bound_depth = {};
    }

    void volumetric_fog_pass::rebuild_bind_groups(view_data& view, gpu::texture scene_depth)
    {
        auto& gpu = *m_device;

        view.release_bind_groups();

        auto texture_entry = [](uint32_t binding, gpu::texture texture_handle)
        {
            gpu::binding_value value{};
            value.binding = binding;
            value.kind = gpu::binding_kind::texture;
            value.texture_value = texture_handle;
            return value;
        };
        auto buffer_entry = [](uint32_t binding, gpu::buffer buffer_handle)
        {
            gpu::binding_value value{};
            value.binding = binding;
            value.kind = gpu::binding_kind::uniform_buffer;
            value.buffer_value = buffer_handle;
            return value;
        };

        gpu::bind_group_descriptor march_descriptor{};
        march_descriptor.layout = m_march_layout;
        march_descriptor.entries.push_back(buffer_entry(gpu::shader_bindings::volumetric_fog_params, view.params_ubo));
        march_descriptor.entries.push_back(texture_entry(gpu::shader_bindings::volumetric_fog_depth, scene_depth));
        view.march_bind_group = gpu.create_bind_group(march_descriptor);

        gpu::bind_group_descriptor upsample_descriptor{};
        upsample_descriptor.layout = m_upsample_layout;
        upsample_descriptor.entries.push_back(texture_entry(0, view.march_texture));
        upsample_descriptor.entries.push_back(texture_entry(1, scene_depth));
        upsample_descriptor.entries.push_back(buffer_entry(2, view.params_ubo));
        view.upsample_bind_group = gpu.create_bind_group(upsample_descriptor);

        gpu::bind_group_descriptor composite_descriptor{};
        composite_descriptor.layout = m_composite_layout;
        composite_descriptor.entries.push_back(texture_entry(0, view.upsample_texture));
        view.composite_bind_group = gpu.create_bind_group(composite_descriptor);

        view.bound_depth = scene_depth;
    }

    void volumetric_fog_pass::upload_params(const frame_context& ctx, const view_data& view)
    {
        auto& gpu = *view.device;

        // The noise only moves while temporal AA is there to average it;
        // without it a static dither reads better than one that crawls
        // every frame. The projection is the camera's own, unjittered: the
        // upsample only reads its depth rows, which the jitter leaves alone.
        const float frame_offset =
            ctx.post.taa.enabled ? static_cast<float>(ctx.frame_index % noise_frame_period) : 0.0f;
        const core::math::mat4 inverse_projection = core::math::inverse(ctx.active_camera->projection);
        const std::array<float, params_floats> params =
            pack_params(ctx.post.volumetric, frame_offset, inverse_projection);
        gpu.write_buffer(view.params_ubo, params.data(), params_ubo_size, 0);
    }

    void volumetric_fog_pass::prepare(const frame_context& ctx)
    {
        const volumetric_fog_settings& settings = ctx.post.volumetric;

        // Off (the default), no camera, no scene pass to take the view
        // from, no depth to march toward, or no medium to march through:
        // draw nothing, leaving the scene colour exactly as the scene and
        // skybox passes wrote it. The medium is the height fog, so a zero
        // height density means empty air.
        const scene_view_data* scene_view = ctx.resources->find(frame_resources::scene_view);
        const gpu::texture scene_depth = ctx.resources->get(frame_resources::scene_depth);
        m_draws = volumetric_fog_active(settings) && ctx.active_camera != nullptr && scene_view != nullptr &&
                  scene_depth.valid() && ctx.fog.height_density > 0.0f;
        if (!m_draws)
        {
            return;
        }
        m_frame_group = scene_view->frame_group;
        m_target = ctx.resources->get(frame_resources::scene_color).target;

        // The view's targets follow the view's size.
        view_data& view = ctx.view->state<view_data>(*this, *m_device);
        if (view.width != ctx.viewport_width || view.height != ctx.viewport_height)
        {
            view.resize(ctx.viewport_width, ctx.viewport_height);
        }

        upload_params(ctx, view);

        // Rebind when the scene depth changes (a resize recreates it) or a
        // resize dropped the groups — the first drawn frame included.
        if (scene_depth != view.bound_depth || !view.march_bind_group.valid())
        {
            rebuild_bind_groups(view, scene_depth);
        }
    }

    void volumetric_fog_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        const view_data* view = m_draws ? ctx.view->find_state<view_data>(*this) : nullptr;
        if (view == nullptr)
        {
            return;
        }

        auto draw_fullscreen = [&](gpu::render_pass_encoder& pass_encoder)
        {
            pass_encoder.set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder.draw(3);
        };

        // 1. March the medium at half resolution. The fullscreen triangle
        //    covers every texel; the clear (to no fog: nothing scattered,
        //    full transmittance) only keeps the target in a known state.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = view->march_target;
            descriptor.color[0].load = gpu::load_op::clear;
            descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_march_pipeline);
            pass_encoder->set_bind_group(0, m_frame_group);
            pass_encoder->set_bind_group(1, view->march_bind_group);
            draw_fullscreen(*pass_encoder);
            pass_encoder->end();
        }

        // 2. Depth-aware upsample to full resolution.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = view->upsample_target;
            descriptor.color[0].load = gpu::load_op::clear;
            descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_upsample_pipeline);
            pass_encoder->set_bind_group(0, view->upsample_bind_group);
            draw_fullscreen(*pass_encoder);
            pass_encoder->end();
        }

        // 3. Blend over the scene colour. Loading (not clearing) keeps the
        //    scene the blend attenuates; depth is off, so the pass leaves
        //    the scene target's depth attachment alone.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_target;
            descriptor.color[0].load = gpu::load_op::load;
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_composite_pipeline);
            pass_encoder->set_bind_group(0, view->composite_bind_group);
            draw_fullscreen(*pass_encoder);
            pass_encoder->end();
        }
    }
} // namespace rendering_engine
