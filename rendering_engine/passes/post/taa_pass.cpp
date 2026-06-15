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

#include <rendering_engine/passes/post/taa_pass.hpp>

#include <array>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Steady-state weight of the reprojected history in the blend. 0.9
    // keeps ten frames of accumulation in flight — enough to resolve the
    // 16-sample Halton jitter into a smooth, supersampled image while
    // still responding to change within a few frames. The neighbourhood
    // clamp below is what stops that long tail from ghosting under motion.
    constexpr float taa_feedback = 0.9f;

    // The reprojection-free temporal resolve. The current jittered frame
    // is blended with last frame's accumulated result; because the
    // projection jitter moves the sub-pixel sample location every frame,
    // that blend converges to a supersampled image on a static view. To
    // keep moving content from smearing the stale history is first
    // constrained to the 3x3 colour box around the current texel (the
    // standard neighbourhood clamp), so any history that has drifted
    // outside what the present frame plausibly contains is pulled back in.
    //
    // u_taa.params.xy is (1/width, 1/height) — the per-texel step the
    // neighbourhood taps walk by — and params.z is the history feedback
    // weight, baked to 0 on the first frame (history undefined) and to
    // taa_feedback thereafter.
    const std::string resolve_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D currentColor;
        layout(set = 0, binding = 1) uniform sampler2D historyColor;

        layout(set = 0, binding = 2, std140) uniform Taa
        {
            vec4 params; // x = 1/width, y = 1/height, z = history feedback, w unused
        } u_taa;

        void main()
        {
            vec2 inv = u_taa.params.xy;

            vec3 current = texture(currentColor, texCoord).rgb;

            // 3x3 neighbourhood min/max of the current frame: the colour
            // box the history is clamped into before blending.
            vec3 box_min = current;
            vec3 box_max = current;
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    if (x == 0 && y == 0)
                    {
                        continue;
                    }
                    vec3 s = texture(currentColor, texCoord + vec2(x, y) * inv).rgb;
                    box_min = min(box_min, s);
                    box_max = max(box_max, s);
                }
            }

            vec3 history = texture(historyColor, texCoord).rgb;
            history = clamp(history, box_min, box_max);

            vec3 resolved = mix(current, history, u_taa.params.z);
            fragColor = vec4(resolved, 1.0);
        }
)fs";

    // History store: a straight copy of the resolved frame into the
    // history target so the next frame's resolve can read it.
    const std::string copy_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D src;

        void main()
        {
            fragColor = texture(src, texCoord);
        }
)fs";

    // std140 rounds the single vec4 block up to a 16-byte allocation.
    constexpr size_t taa_ubo_size = 16;
} // namespace

namespace rendering_engine
{
    taa_pass::taa_pass(gpu::texture current_color, uint32_t width, uint32_t height)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Degenerate backbuffer (no settings, zero-sized window): leave the
        // pass disabled so output_texture() reports invalid and the caller
        // keeps sampling the raw tonemap output.
        if (width == 0 || height == 0)
        {
            return;
        }

        // -- Shaders --------------------------------------------------
        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(fullscreen_triangle_vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor resolve_descriptor{};
        resolve_descriptor.stage = gpu::shader_stage::fragment;
        resolve_descriptor.spirv = gpu::compile_glsl_to_spirv(resolve_shader, gpu::shader_stage::fragment);
        m_resolve_shader = gpu.create_shader_module(resolve_descriptor);

        gpu::shader_module_descriptor copy_descriptor{};
        copy_descriptor.stage = gpu::shader_stage::fragment;
        copy_descriptor.spirv = gpu::compile_glsl_to_spirv(copy_shader, gpu::shader_stage::fragment);
        m_copy_shader = gpu.create_shader_module(copy_descriptor);

        // -- Fullscreen-triangle vertex buffer ------------------------
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // -- Resolve params UBO: texel step + history feedback --------
        // The feedback starts at 0 so the first frame ignores the still
        // undefined history; record() bumps it to taa_feedback once the
        // history target has been populated.
        m_inv_width = 1.0f / static_cast<float>(width);
        m_inv_height = 1.0f / static_cast<float>(height);
        const std::array<float, 4> initial_params = {m_inv_width, m_inv_height, 0.0f, 0.0f};
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = taa_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        ubo_descriptor.initial_data = initial_params.data();
        m_resolve_ubo = gpu.create_buffer(ubo_descriptor);

        // -- Bind-group layouts ---------------------------------------
        gpu::bind_group_layout_descriptor resolve_layout{};
        resolve_layout.entries.push_back({0, gpu::binding_kind::texture});
        resolve_layout.entries.push_back({1, gpu::binding_kind::texture});
        resolve_layout.entries.push_back({2, gpu::binding_kind::uniform_buffer});
        m_resolve_layout = gpu.create_bind_group_layout(resolve_layout);

        gpu::bind_group_layout_descriptor copy_layout{};
        copy_layout.entries.push_back({0, gpu::binding_kind::texture});
        m_copy_layout = gpu.create_bind_group_layout(copy_layout);

        // -- Targets --------------------------------------------------
        // Both rgba8, no depth: the post chain runs depth-disabled and the
        // image is already tonemapped LDR at this point.
        auto make_target = [&]()
        {
            gpu::render_target_descriptor descriptor{};
            descriptor.color_format = gpu::texture_format::rgba8_unorm;
            descriptor.width = width;
            descriptor.height = height;
            descriptor.with_depth = false;
            return gpu.create_render_target(descriptor);
        };

        m_history_target = make_target();
        m_history_texture = gpu.render_target_color_texture(m_history_target);
        m_resolve_target = make_target();
        m_resolve_texture = gpu.render_target_color_texture(m_resolve_target);

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

        auto make_pipeline = [&](gpu::shader_module fragment, gpu::bind_group_layout layout)
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

        m_resolve_pipeline = make_pipeline(m_resolve_shader, m_resolve_layout);
        m_copy_pipeline = make_pipeline(m_copy_shader, m_copy_layout);

        // -- Bind groups ----------------------------------------------
        gpu::bind_group_descriptor resolve_bind_group_descriptor{};
        resolve_bind_group_descriptor.layout = m_resolve_layout;

        gpu::binding_value current_slot{};
        current_slot.binding = 0;
        current_slot.kind = gpu::binding_kind::texture;
        current_slot.texture_value = current_color;
        resolve_bind_group_descriptor.entries.push_back(current_slot);

        gpu::binding_value history_slot{};
        history_slot.binding = 1;
        history_slot.kind = gpu::binding_kind::texture;
        history_slot.texture_value = m_history_texture;
        resolve_bind_group_descriptor.entries.push_back(history_slot);

        gpu::binding_value params_slot{};
        params_slot.binding = 2;
        params_slot.kind = gpu::binding_kind::uniform_buffer;
        params_slot.buffer_value = m_resolve_ubo;
        resolve_bind_group_descriptor.entries.push_back(params_slot);

        m_resolve_bind_group = gpu.create_bind_group(resolve_bind_group_descriptor);

        gpu::bind_group_descriptor copy_bind_group_descriptor{};
        copy_bind_group_descriptor.layout = m_copy_layout;

        gpu::binding_value src_slot{};
        src_slot.binding = 0;
        src_slot.kind = gpu::binding_kind::texture;
        src_slot.texture_value = m_resolve_texture;
        copy_bind_group_descriptor.entries.push_back(src_slot);

        m_copy_bind_group = gpu.create_bind_group(copy_bind_group_descriptor);

        m_enabled = true;
    }

    taa_pass::~taa_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        if (m_copy_bind_group.valid())
        {
            gpu.destroy(m_copy_bind_group);
            m_copy_bind_group = {};
        }
        if (m_resolve_bind_group.valid())
        {
            gpu.destroy(m_resolve_bind_group);
            m_resolve_bind_group = {};
        }
        // Each render target owns its colour attachment, so destroying the
        // target releases the texture too.
        if (m_resolve_target.valid())
        {
            gpu.destroy(m_resolve_target);
            m_resolve_target = {};
            m_resolve_texture = {};
        }
        if (m_history_target.valid())
        {
            gpu.destroy(m_history_target);
            m_history_target = {};
            m_history_texture = {};
        }
        if (m_copy_pipeline.valid())
        {
            gpu.destroy(m_copy_pipeline);
            m_copy_pipeline = {};
        }
        if (m_resolve_pipeline.valid())
        {
            gpu.destroy(m_resolve_pipeline);
            m_resolve_pipeline = {};
        }
        if (m_copy_layout.valid())
        {
            gpu.destroy(m_copy_layout);
            m_copy_layout = {};
        }
        if (m_resolve_layout.valid())
        {
            gpu.destroy(m_resolve_layout);
            m_resolve_layout = {};
        }
        if (m_resolve_ubo.valid())
        {
            gpu.destroy(m_resolve_ubo);
            m_resolve_ubo = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
        if (m_copy_shader.valid())
        {
            gpu.destroy(m_copy_shader);
            m_copy_shader = {};
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

    gpu::texture taa_pass::output_texture() const
    {
        return m_resolve_texture;
    }

    void taa_pass::record(gpu::command_encoder& encoder, const frame_context&)
    {
        if (!m_enabled)
        {
            return;
        }

        auto& gpu = *runtime::current_engine().gpu;

        auto draw_fullscreen =
            [&](gpu::render_pass_encoder* pass_encoder, gpu::pipeline pipeline, gpu::bind_group bind_group)
        {
            pass_encoder->set_pipeline(pipeline);
            pass_encoder->set_bind_group(0, bind_group);
            pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
            pass_encoder->draw(3, 0);
        };

        // 1. Resolve: blend the current LDR frame with the clamped history
        //    into the resolve target the next pass (FXAA) samples.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_resolve_target;
            descriptor.color.load = gpu::load_op::clear;
            descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            draw_fullscreen(pass_encoder.get(), m_resolve_pipeline, m_resolve_bind_group);
            pass_encoder->end();
        }

        // 2. Store the resolved frame into the history target for the next
        //    frame's resolve to read.
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_history_target;
            descriptor.color.load = gpu::load_op::clear;
            descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
            descriptor.use_depth = false;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            draw_fullscreen(pass_encoder.get(), m_copy_pipeline, m_copy_bind_group);
            pass_encoder->end();
        }

        // The first frame resolved against an undefined history with the
        // feedback pinned to 0 (current frame only). Now that the history
        // target holds a real frame, switch to the steady-state feedback so
        // subsequent frames accumulate.
        if (m_first_frame)
        {
            m_first_frame = false;
            // Rewrite the whole vec4 so the baked texel step (xy) is kept
            // alongside the now-live feedback weight (z).
            const std::array<float, 4> params = {m_inv_width, m_inv_height, taa_feedback, 0.0f};
            gpu.write_buffer(m_resolve_ubo, params.data(), taa_ubo_size, 0);
        }
    }
} // namespace rendering_engine
