/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
#include <runtime/engine.hpp>

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
    motion_blur_pass::motion_blur_pass(uint32_t width, uint32_t height)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Degenerate backbuffer (no settings, zero-sized window): leave the
        // pass disabled so draws() is false and the chain reads the scene
        // colour directly.
        if (width == 0 || height == 0)
        {
            return;
        }

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

        // Rewritten every drawn frame (the noise offset moves while
        // temporal AA runs), so host-visible: the device keeps one copy
        // per frame in flight.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = params_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_params_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_layout_descriptor layout{};
        layout.entries.push_back({0, gpu::binding_kind::texture});
        layout.entries.push_back({1, gpu::binding_kind::texture});
        layout.entries.push_back({2, gpu::binding_kind::uniform_buffer});
        m_layout = gpu.create_bind_group_layout(layout);

        // The output target waits for prepare(): motion blur is off by
        // default, and a full-resolution target it never draws into is not
        // worth holding.
        m_width = width;
        m_height = height;

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

        // The bind group samples the scene colour and the motion vectors,
        // which arrive through the frame context; record() builds it on
        // the first drawn frame and rebuilds it whenever either changes.
        m_enabled = true;
    }

    motion_blur_pass::~motion_blur_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        if (m_bind_group.valid())
        {
            gpu.destroy(m_bind_group);
            m_bind_group = {};
        }
        // The target owns its colour attachment, so destroying it releases
        // the texture too.
        if (m_target.valid())
        {
            gpu.destroy(m_target);
            m_target = {};
            m_texture = {};
        }
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
        if (m_params_ubo.valid())
        {
            gpu.destroy(m_params_ubo);
            m_params_ubo = {};
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

    void motion_blur_pass::create_target()
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Same format as the scene colour it stands in for; no depth, the
        // post chain runs depth-disabled.
        gpu::render_target_descriptor descriptor{};
        descriptor.color = {{gpu::texture_format::rgba16_float}};
        descriptor.width = m_width;
        descriptor.height = m_height;
        descriptor.with_depth = false;
        m_target = gpu.create_render_target(descriptor);
        m_texture = gpu.render_target_color_texture(m_target);
    }

    void motion_blur_pass::prepare(const motion_blur_settings& settings)
    {
        if (m_enabled && !m_target.valid() && motion_blur_active(settings))
        {
            create_target();
        }
    }

    void motion_blur_pass::resize(uint32_t width, uint32_t height)
    {
        if (!m_enabled || width == 0 || height == 0)
        {
            return;
        }
        m_width = width;
        m_height = height;
        if (!m_target.valid())
        {
            // Not allocated yet: prepare() creates it at this size.
            return;
        }
        auto& gpu = *runtime::current_engine().gpu;

        // Create the replacement before releasing the old target so the
        // handle published through frame_context::hdr_color_texture changes
        // and its consumers rebind. The release is safe here: resize runs
        // between frames, and a deferred-execution backend retires the
        // attachment only once the last command buffer that used it has
        // finished.
        const gpu::render_target old_target = m_target;
        create_target();
        if (old_target.valid())
        {
            gpu.destroy(old_target);
        }
    }

    bool motion_blur_pass::draws(const frame_context& ctx) const
    {
        return m_enabled && m_target.valid() && motion_blur_active(ctx.post.motion_blur) &&
               ctx.velocity_texture.valid();
    }

    gpu::render_target motion_blur_pass::output_target() const
    {
        return m_target;
    }

    gpu::texture motion_blur_pass::output_texture() const
    {
        return m_texture;
    }

    void motion_blur_pass::rebuild_bind_group(gpu::texture scene_color, gpu::texture velocity)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Safe mid-frame: the device defers the destroy until the command
        // buffer that may still reference the old group has retired.
        if (m_bind_group.valid())
        {
            gpu.destroy(m_bind_group);
            m_bind_group = {};
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
        params_slot.buffer_value = m_params_ubo;
        descriptor.entries.push_back(params_slot);

        m_bind_group = gpu.create_bind_group(descriptor);
        m_bound_color = scene_color;
        m_bound_velocity = velocity;
    }

    void motion_blur_pass::upload_params(const frame_context& ctx)
    {
        auto& gpu = *runtime::current_engine().gpu;
        const motion_blur_settings& settings = ctx.post.motion_blur;

        // The noise only moves while temporal AA is there to average it;
        // without it a moving pattern would crawl.
        const float noise_frame =
            ctx.post.taa.enabled ? static_cast<float>(ctx.frame_index % noise_frame_period) : 0.0f;
        const auto width = static_cast<float>(m_width);
        const auto height = static_cast<float>(m_height);
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
        gpu.write_buffer(m_params_ubo, params.data(), params_ubo_size, 0);
    }

    void motion_blur_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // The same test context::render published frame_context::hdr_color_*
        // by: when it fails, the chain after this pass reads the scene
        // colour and there is nothing to do.
        if (!draws(ctx))
        {
            return;
        }

        // Written here, after begin_frame waited for the frame that last
        // read this frame slot's copy of the buffer.
        upload_params(ctx);

        // Bind this frame's scene colour and motion vectors. Both handles
        // are stable until a resize recreates their targets, so compare
        // against what the group was built with and rebuild on change —
        // the first drawn frame included.
        if (ctx.scene_color_texture != m_bound_color || ctx.velocity_texture != m_bound_velocity ||
            !m_bind_group.valid())
        {
            rebuild_bind_group(ctx.scene_color_texture, ctx.velocity_texture);
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
