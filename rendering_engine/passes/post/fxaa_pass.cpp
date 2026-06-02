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

#include <rendering_engine/passes/post/fxaa_pass.hpp>

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
    // The canonical compact FXAA (Timothy Lottes, NVIDIA whitepaper),
    // operating on the gamma-encoded LDR image tonemap leaves behind.
    // A 3x3 luma neighbourhood around the centre texel chooses the edge
    // direction; two pairs of bilinear taps along that direction give a
    // narrow (rgbA) and a wider (rgbB) blend, and the wider one is kept
    // only while its luma stays inside the local min/max so high-contrast
    // detail is not over-smeared. clamp_edge sampling on the LDR
    // render-target texture keeps the off-centre taps well-defined at the
    // borders, and its linear filter is what makes the sub-texel taps
    // actually average neighbours.
    //
    // u_fxaa.rcp_frame.xy is (1/width, 1/height) — the per-texel step the
    // edge search walks by — baked once at construction.
    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D ldrColor;

        layout(set = 0, binding = 1, std140) uniform Fxaa
        {
            vec4 rcp_frame; // xy = 1/resolution, zw unused
        } u_fxaa;

        // Largest distance, in texels, the edge blend may reach.
        const float span_max = 8.0;
        // Scales the neighbourhood luma into the directional reduce term so
        // near-uniform regions stop searching; the floor keeps the divide
        // below well-conditioned on a perfectly flat patch.
        const float reduce_mul = 1.0 / 8.0;
        const float reduce_min = 1.0 / 128.0;

        void main()
        {
            vec2 inv = u_fxaa.rcp_frame.xy;

            vec3 rgb_m = texture(ldrColor, texCoord).rgb;
            vec3 rgb_nw = texture(ldrColor, texCoord + vec2(-1.0, -1.0) * inv).rgb;
            vec3 rgb_ne = texture(ldrColor, texCoord + vec2(1.0, -1.0) * inv).rgb;
            vec3 rgb_sw = texture(ldrColor, texCoord + vec2(-1.0, 1.0) * inv).rgb;
            vec3 rgb_se = texture(ldrColor, texCoord + vec2(1.0, 1.0) * inv).rgb;

            const vec3 luma_weights = vec3(0.299, 0.587, 0.114);
            float luma_m = dot(rgb_m, luma_weights);
            float luma_nw = dot(rgb_nw, luma_weights);
            float luma_ne = dot(rgb_ne, luma_weights);
            float luma_sw = dot(rgb_sw, luma_weights);
            float luma_se = dot(rgb_se, luma_weights);

            float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
            float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));

            // Edge direction is the gradient of the corner lumas, rotated
            // 90 degrees so the blend runs *along* the edge, not across it.
            vec2 dir;
            dir.x = -((luma_nw + luma_ne) - (luma_sw + luma_se));
            dir.y = ((luma_nw + luma_sw) - (luma_ne + luma_se));

            float dir_reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * (0.25 * reduce_mul), reduce_min);
            float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
            dir = clamp(dir * rcp_dir_min, vec2(-span_max), vec2(span_max)) * inv;

            // Narrow blend: the two inner taps (+/- 1/6 of the span).
            vec3 rgb_a = 0.5 * (texture(ldrColor, texCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
                                texture(ldrColor, texCoord + dir * (2.0 / 3.0 - 0.5)).rgb);
            // Wider blend: average in the two outer taps (+/- 1/2 of span).
            vec3 rgb_b = rgb_a * 0.5 + 0.25 * (texture(ldrColor, texCoord + dir * -0.5).rgb +
                                               texture(ldrColor, texCoord + dir * 0.5).rgb);

            float luma_b = dot(rgb_b, luma_weights);
            if (luma_b < luma_min || luma_b > luma_max)
            {
                fragColor = vec4(rgb_a, 1.0);
            }
            else
            {
                fragColor = vec4(rgb_b, 1.0);
            }
        }
)fs";
} // namespace

namespace rendering_engine
{
    fxaa_pass::fxaa_pass(gpu::texture input_color, uint32_t width, uint32_t height)
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(fullscreen_triangle_vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_glsl_to_spirv(fragment_shader, gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        // Three vec2 vertices for the oversized fullscreen triangle.
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // Per-texel edge step baked once from the backbuffer size. A
        // degenerate target bakes a zero step, collapsing every off-centre
        // tap onto the centre texel so the pass copies straight through.
        // std140 rounds the vec2 up to a 16-byte vec4 allocation.
        const float rcp_x = (width != 0) ? 1.0f / static_cast<float>(width) : 0.0f;
        const float rcp_y = (height != 0) ? 1.0f / static_cast<float>(height) : 0.0f;
        const std::array<float, 4> rcp_frame = {rcp_x, rcp_y, 0.0f, 0.0f};
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = 16;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::static_data;
        ubo_descriptor.initial_data = rcp_frame.data();
        m_rcp_frame_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::texture});
        input_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        gpu::bind_group_descriptor input_bind_group_descriptor{};
        input_bind_group_descriptor.layout = m_input_layout;

        gpu::binding_value ldr_color_slot{};
        ldr_color_slot.binding = 0;
        ldr_color_slot.kind = gpu::binding_kind::texture;
        ldr_color_slot.texture_value = input_color;
        input_bind_group_descriptor.entries.push_back(ldr_color_slot);

        gpu::binding_value rcp_frame_slot{};
        rcp_frame_slot.binding = 1;
        rcp_frame_slot.kind = gpu::binding_kind::uniform_buffer;
        rcp_frame_slot.buffer_value = m_rcp_frame_ubo;
        input_bind_group_descriptor.entries.push_back(rcp_frame_slot);

        m_input_bind_group = gpu.create_bind_group(input_bind_group_descriptor);

        // Fullscreen triangle: depth disabled, blend disabled, no culling
        // so the triangle's winding is irrelevant. The vertex shader reads
        // a single vec2 attribute from @ref m_vertex_buffer.
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
        pipeline_descriptor.bind_group_layouts.push_back(m_input_layout);

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    fxaa_pass::~fxaa_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_input_bind_group.valid())
        {
            gpu.destroy(m_input_bind_group);
            m_input_bind_group = {};
        }
        if (m_input_layout.valid())
        {
            gpu.destroy(m_input_layout);
            m_input_layout = {};
        }
        if (m_rcp_frame_ubo.valid())
        {
            gpu.destroy(m_rcp_frame_ubo);
            m_rcp_frame_ubo = {};
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

    void fxaa_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.swapchain_target;
        // The fullscreen triangle covers every pixel; clearing is strictly
        // redundant but cheap and keeps the swapchain in a known state.
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3, 0);
        pass_encoder->end();
    }
} // namespace rendering_engine
