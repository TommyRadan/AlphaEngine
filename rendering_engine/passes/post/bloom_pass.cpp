// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/post/bloom_pass.hpp>

#include <algorithm>
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
    // Number of mips in the blur pyramid. Five levels spread the glow
    // across roughly a thirtieth of the screen at the coarsest mip while
    // keeping the target count modest. The pyramid stops growing once a
    // dimension clamps to one texel, so smaller windows simply reuse the
    // floor resolution for their tail levels.
    constexpr uint32_t bloom_mip_count = 5;

    // The compiled-in threshold, knee and strength defaults live on
    // bloom_settings (rendering_engine/post_settings.hpp), which each
    // view's bloom_pass::view_data::settings is seeded from; see that
    // struct's doc comment for what each one does.

    // Tiny epsilon guarding the divisions in the bright-pass knee and the
    // luminance normalise.
    constexpr float bloom_epsilon = 1.0e-4f;

    // std140 rounds every one-to-four-float block up to a 16-byte
    // allocation, so each baked params UBO is a single vec4.
    constexpr size_t params_ubo_size = 16;

    // The bright-pass, separable-blur and composite fragment stages are
    // shaders/passes/bloom_{threshold,blur,composite}.frag.glsl, drawn over
    // the shared fullscreen triangle.
} // namespace

namespace rendering_engine
{
    bloom_pass::bloom_pass(gpu::device& device) : m_device(&device)
    {
        auto& gpu = *m_device;

        // -- Shaders --------------------------------------------------
        m_vertex_shader =
            gpu::create_library_shader_module(gpu, "passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_threshold_shader =
            gpu::create_library_shader_module(gpu, "passes/bloom_threshold.frag.glsl", gpu::shader_stage::fragment);
        m_blur_shader =
            gpu::create_library_shader_module(gpu, "passes/bloom_blur.frag.glsl", gpu::shader_stage::fragment);
        m_composite_shader =
            gpu::create_library_shader_module(gpu, "passes/bloom_composite.frag.glsl", gpu::shader_stage::fragment);

        // -- Fullscreen-triangle vertex buffer ------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Shared {texture, ubo} layout -----------------------------
        gpu::bind_group_layout_descriptor io_layout{};
        io_layout.entries.push_back({0, gpu::binding_kind::texture});
        io_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_io_layout = gpu.create_bind_group_layout(io_layout);

        // -- Fixed-function state shared by the fullscreen stages ------
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

        // Threshold and blur write into a freshly cleared target, so no
        // blending; the composite accumulates each mip on top of the
        // scene already in the target, so it blends additively.
        gpu::blend_state opaque_blend{};
        opaque_blend.enabled = false;

        gpu::blend_state additive_blend{};
        additive_blend.enabled = true;
        additive_blend.src = gpu::blend_factor::one;
        additive_blend.dst = gpu::blend_factor::one;
        additive_blend.op = gpu::blend_op::add;

        auto make_pipeline = [&](gpu::shader_module fragment, const gpu::blend_state& blend)
        {
            gpu::pipeline_descriptor descriptor{};
            descriptor.vertex_shader = m_vertex_shader;
            descriptor.fragment_shader = fragment;
            descriptor.vertex_buffers.push_back(vertex_layout);
            descriptor.depth = depth;
            descriptor.blend = blend;
            descriptor.rasterizer = rasterizer;
            descriptor.bind_group_layouts.push_back(m_io_layout);
            return gpu.create_pipeline(descriptor);
        };

        m_threshold_pipeline = make_pipeline(m_threshold_shader, opaque_blend);
        m_blur_pipeline = make_pipeline(m_blur_shader, opaque_blend);
        m_composite_pipeline = make_pipeline(m_composite_shader, additive_blend);

        // Each view's bright-pass target, threshold params and blur
        // pyramid are built by prepare() the first time the view blooms.
    }

    gpu::render_target bloom_pass::create_target(uint32_t width, uint32_t height) const
    {
        auto& gpu = *m_device;
        gpu::render_target_descriptor descriptor{};
        descriptor.color = {{gpu::texture_format::rgba16_float}};
        descriptor.width = width;
        descriptor.height = height;
        descriptor.with_depth = false;
        return gpu.create_render_target(descriptor);
    }

    gpu::buffer bloom_pass::create_params_ubo(const std::array<float, 4>& values) const
    {
        auto& gpu = *m_device;
        gpu::buffer_descriptor descriptor{};
        descriptor.size = params_ubo_size;
        descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        descriptor.hint = gpu::buffer_usage_hint::static_data;
        descriptor.initial_data = values.data();
        return gpu.create_buffer(descriptor);
    }

    gpu::bind_group bloom_pass::create_bind_group(gpu::texture input, gpu::buffer ubo) const
    {
        auto& gpu = *m_device;
        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_io_layout;

        gpu::binding_value texture_slot{};
        texture_slot.binding = 0;
        texture_slot.kind = gpu::binding_kind::texture;
        texture_slot.texture_value = input;
        descriptor.entries.push_back(texture_slot);

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = 1;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = ubo;
        descriptor.entries.push_back(ubo_slot);

        return gpu.create_bind_group(descriptor);
    }

    void bloom_pass::create_pyramid(view_data& view, uint32_t width, uint32_t height)
    {
        auto& gpu = *m_device;

        // -- Threshold params -----------------------------------------
        // Size-independent, so baked from the view's settings (prepare()
        // rewrites it in place on a runtime change); the bright-pass bind
        // group that pairs it with the scene colour is built by prepare()
        // once it looks the handle up.
        if (!view.threshold_ubo.valid())
        {
            const float knee = view.settings.threshold * view.settings.knee;
            view.threshold_ubo =
                create_params_ubo({view.settings.threshold, knee, 2.0f * knee, 1.0f / (4.0f * knee + bloom_epsilon)});
        }

        // -- Bright pass: half-resolution threshold output ------------
        const uint32_t base_width = std::max(1u, width / 2);
        const uint32_t base_height = std::max(1u, height / 2);
        view.bright_target = create_target(base_width, base_height);
        view.bright_texture = gpu.render_target_color_texture(view.bright_target);

        // -- Blur pyramid ---------------------------------------------
        // Per-level weights fall off linearly toward the coarser mips and
        // are normalised so they sum to the view's settings.strength
        // (whatever was last applied, or its compiled-in default the first
        // time this runs): the wide, blurry low-frequency levels contribute
        // less than the tight bright core.
        float weight_total = 0.0f;
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            weight_total += static_cast<float>(bloom_mip_count - i);
        }

        view.levels.reserve(bloom_mip_count);
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            bloom_level level{};
            level.width = std::max(1u, base_width >> i);
            level.height = std::max(1u, base_height >> i);

            level.horizontal_target = create_target(level.width, level.height);
            level.horizontal_texture = gpu.render_target_color_texture(level.horizontal_target);
            level.vertical_target = create_target(level.width, level.height);
            level.vertical_texture = gpu.render_target_color_texture(level.vertical_target);

            // Horizontal blur samples the previous level's fully blurred
            // mip (or the bright pass for level 0); the per-tap step is
            // that source's texel width, so a smaller target downsamples
            // as it blurs.
            const gpu::texture horizontal_input = (i == 0) ? view.bright_texture : view.levels[i - 1].vertical_texture;
            const uint32_t source_width = (i == 0) ? base_width : view.levels[i - 1].width;
            level.blur_horizontal_ubo = create_params_ubo({1.0f / static_cast<float>(source_width), 0.0f, 0.0f, 0.0f});
            level.blur_horizontal_bind_group = create_bind_group(horizontal_input, level.blur_horizontal_ubo);

            // Vertical blur samples the horizontal result at this level's
            // own resolution.
            level.blur_vertical_ubo = create_params_ubo({0.0f, 1.0f / static_cast<float>(level.height), 0.0f, 0.0f});
            level.blur_vertical_bind_group = create_bind_group(level.horizontal_texture, level.blur_vertical_ubo);

            const float weight = view.settings.strength * static_cast<float>(bloom_mip_count - i) / weight_total;
            level.weight_ubo = create_params_ubo({weight, 0.0f, 0.0f, 0.0f});
            level.composite_bind_group = create_bind_group(level.vertical_texture, level.weight_ubo);

            view.levels.push_back(level);
        }
        view.width = width;
        view.height = height;
    }

    bloom_pass::view_data::~view_data()
    {
        release_pyramid();
        if (threshold_bind_group.valid())
        {
            device->destroy(threshold_bind_group);
        }
        if (threshold_ubo.valid())
        {
            device->destroy(threshold_ubo);
        }
    }

    void bloom_pass::view_data::release_pyramid()
    {
        auto& gpu = *device;

        // Bind groups first, then the buffers / targets they reference.
        for (auto& level : levels)
        {
            if (level.composite_bind_group.valid())
            {
                gpu.destroy(level.composite_bind_group);
            }
            if (level.blur_vertical_bind_group.valid())
            {
                gpu.destroy(level.blur_vertical_bind_group);
            }
            if (level.blur_horizontal_bind_group.valid())
            {
                gpu.destroy(level.blur_horizontal_bind_group);
            }
            if (level.weight_ubo.valid())
            {
                gpu.destroy(level.weight_ubo);
            }
            if (level.blur_vertical_ubo.valid())
            {
                gpu.destroy(level.blur_vertical_ubo);
            }
            if (level.blur_horizontal_ubo.valid())
            {
                gpu.destroy(level.blur_horizontal_ubo);
            }
            // Each render target owns its colour attachment, so destroying
            // the target releases the texture too.
            if (level.vertical_target.valid())
            {
                gpu.destroy(level.vertical_target);
            }
            if (level.horizontal_target.valid())
            {
                gpu.destroy(level.horizontal_target);
            }
        }
        levels.clear();

        if (bright_target.valid())
        {
            gpu.destroy(bright_target);
            bright_target = {};
            bright_texture = {};
        }
        width = 0;
        height = 0;
    }

    void bloom_pass::rebuild_threshold_bind_group(view_data& view, gpu::texture scene_color)
    {
        auto& gpu = *m_device;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old group has retired.
        if (view.threshold_bind_group.valid())
        {
            gpu.destroy(view.threshold_bind_group);
            view.threshold_bind_group = {};
        }
        view.threshold_bind_group = create_bind_group(scene_color, view.threshold_ubo);
        view.bound_scene_color = scene_color;
    }

    void bloom_pass::write_threshold_ubo(view_data& view, float threshold, float knee)
    {
        const float knee_width = threshold * knee;
        const std::array<float, 4> bytes = {
            threshold, knee_width, 2.0f * knee_width, 1.0f / (4.0f * knee_width + bloom_epsilon)};
        view.device->write_buffer(view.threshold_ubo, bytes.data(), params_ubo_size, 0);
    }

    void bloom_pass::write_weights(view_data& view, float strength)
    {
        auto& gpu = *view.device;
        float weight_total = 0.0f;
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            weight_total += static_cast<float>(bloom_mip_count - i);
        }
        for (uint32_t i = 0; i < bloom_mip_count; ++i)
        {
            const float weight = strength * static_cast<float>(bloom_mip_count - i) / weight_total;
            const std::array<float, 4> bytes = {weight, 0.0f, 0.0f, 0.0f};
            gpu.write_buffer(view.levels[i].weight_ubo, bytes.data(), params_ubo_size, 0);
        }
    }

    bloom_pass::~bloom_pass()
    {
        auto& gpu = *m_device;

        if (m_composite_pipeline.valid())
        {
            gpu.destroy(m_composite_pipeline);
            m_composite_pipeline = {};
        }
        if (m_blur_pipeline.valid())
        {
            gpu.destroy(m_blur_pipeline);
            m_blur_pipeline = {};
        }
        if (m_threshold_pipeline.valid())
        {
            gpu.destroy(m_threshold_pipeline);
            m_threshold_pipeline = {};
        }
        if (m_io_layout.valid())
        {
            gpu.destroy(m_io_layout);
            m_io_layout = {};
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
        if (m_blur_shader.valid())
        {
            gpu.destroy(m_blur_shader);
            m_blur_shader = {};
        }
        if (m_threshold_shader.valid())
        {
            gpu.destroy(m_threshold_shader);
            m_threshold_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
    }

    void bloom_pass::prepare(const frame_context& ctx)
    {
        // bloom_settings::enabled early-outs entirely rather than removing
        // this pass from the pass list: the HDR scene colour it would
        // have brightened just flows through untouched to tonemap.
        m_draws = ctx.post.bloom.enabled;
        if (!m_draws)
        {
            return;
        }

        // The view's pyramid follows the view's size. Nothing outside this
        // pass samples it, and every bind group that does is rebuilt with
        // it, so the old pyramid can go before the new one is built; the
        // device retires the attachments only once the last command buffer
        // that sampled them has finished.
        view_data& view = ctx.view->state<view_data>(*this, *m_device);
        if (view.width != ctx.viewport_width || view.height != ctx.viewport_height)
        {
            view.release_pyramid();
            create_pyramid(view, ctx.viewport_width, ctx.viewport_height);
        }

        // Rewrite only the UBO(s) a changed field affects; a caller that
        // never touches frame_context::post leaves every comparison here
        // false forever.
        if (ctx.post.bloom.threshold != view.settings.threshold || ctx.post.bloom.knee != view.settings.knee)
        {
            write_threshold_ubo(view, ctx.post.bloom.threshold, ctx.post.bloom.knee);
            view.settings.threshold = ctx.post.bloom.threshold;
            view.settings.knee = ctx.post.bloom.knee;
        }
        if (ctx.post.bloom.strength != view.settings.strength)
        {
            write_weights(view, ctx.post.bloom.strength);
            view.settings.strength = ctx.post.bloom.strength;
        }

        // Bind this frame's HDR image for the bright pass: the scene
        // colour, or motion blur's output while that runs (it republishes
        // frame_resources::scene_color). The handle only changes when a
        // target is recreated (a resize) or motion blur is toggled, so
        // compare against the one the bind group was built with and
        // rebuild on change — the first frame included.
        const color_target hdr = ctx.resources->get(frame_resources::scene_color);
        m_target = hdr.target;
        if (hdr.texture != view.bound_scene_color || !view.threshold_bind_group.valid())
        {
            rebuild_threshold_bind_group(view, hdr.texture);
        }
    }

    void bloom_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        const view_data* view = m_draws ? ctx.view->find_state<view_data>(*this) : nullptr;
        if (view == nullptr)
        {
            return;
        }

        // Draws a single fullscreen triangle into the currently open pass.
        auto draw_fullscreen =
            [&](gpu::render_pass_encoder* pass_encoder, gpu::pipeline pipeline, gpu::bind_group bind_group)
        {
            pass_encoder->set_pipeline(pipeline);
            pass_encoder->set_bind_group(0, bind_group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3);
        };

        // 1. Bright pass: scene colour → half-res threshold target.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = view->bright_target;
            descriptor.color[0].load = gpu::load_op::clear;
            descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            draw_fullscreen(pass_encoder.get(), m_threshold_pipeline, view->threshold_bind_group);
            pass_encoder->end();
        }

        // 2. Separable Gaussian down the pyramid: horizontal then vertical
        //    per level. begin_render_pass defaults the viewport to each
        //    target's own extent, so the smaller mips downsample for free.
        for (const auto& level : view->levels)
        {
            {
                gpu::render_pass_descriptor descriptor{};
                descriptor.target = level.horizontal_target;
                descriptor.color[0].load = gpu::load_op::clear;
                descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
                descriptor.use_depth = false;

                auto pass_encoder = encoder.begin_render_pass(descriptor);
                draw_fullscreen(pass_encoder.get(), m_blur_pipeline, level.blur_horizontal_bind_group);
                pass_encoder->end();
            }
            {
                gpu::render_pass_descriptor descriptor{};
                descriptor.target = level.vertical_target;
                descriptor.color[0].load = gpu::load_op::clear;
                descriptor.color[0].clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
                descriptor.use_depth = false;

                auto pass_encoder = encoder.begin_render_pass(descriptor);
                draw_fullscreen(pass_encoder.get(), m_blur_pipeline, level.blur_vertical_bind_group);
                pass_encoder->end();
            }
        }

        // 3. Additively composite every blurred mip back into the HDR
        //    image it was extracted from. Loading (not clearing) preserves
        //    the scene the tonemap pass will read; linear sampling upscales
        //    each mip to full resolution as it is composited.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_target;
            descriptor.color[0].load = gpu::load_op::load;
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->set_pipeline(m_composite_pipeline);
            for (const auto& level : view->levels)
            {
                pass_encoder->set_bind_group(0, level.composite_bind_group);
                pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
                pass_encoder->draw(3);
            }
            pass_encoder->end();
        }
    }
} // namespace rendering_engine
