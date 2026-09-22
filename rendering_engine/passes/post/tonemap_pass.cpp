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

#include <rendering_engine/passes/post/tonemap_pass.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    // The curve selection and the gamma-2.2 encode are
    // shaders/passes/tonemap.frag.glsl, drawn over the shared fullscreen
    // triangle. u_tonemap.op matches the rendering_engine::tonemap_operator
    // enumerator values exactly: 0 = none (clamp only), 1 = Reinhard,
    // 2 = ACES filmic.

    // std140 rounds the { float, int } block up to the 16-byte
    // minimum; the float sits at offset 0 and the int at offset 4.
    constexpr std::size_t tonemap_ubo_size = 16;

    std::array<std::byte, tonemap_ubo_size> pack_uniforms(float exposure, rendering_engine::tonemap_operator op)
    {
        std::array<std::byte, tonemap_ubo_size> bytes{};
        const std::int32_t op_value = static_cast<std::int32_t>(op);
        std::memcpy(bytes.data(), &exposure, sizeof(float));
        std::memcpy(bytes.data() + sizeof(float), &op_value, sizeof(std::int32_t));
        return bytes;
    }
} // namespace

namespace rendering_engine
{
    tonemap_pass::tonemap_pass(gpu::texture input_color)
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_library_shader("passes/fullscreen.vert.glsl", gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_library_shader("passes/tonemap.frag.glsl", gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        // Three vec2 vertices for the oversized fullscreen triangle.
        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // The Tonemap UBO holds the exposure scale and the operator
        // selector. Both are live-tunable through set_exposure /
        // set_operator, which rewrite this buffer via write_buffer;
        // a future auto-exposure pass will drive the exposure the same
        // way. std140 lays the float and the int contiguously and
        // rounds the block up to the 16-byte minimum.
        const std::array<std::byte, tonemap_ubo_size> initial = pack_uniforms(m_exposure, m_operator);
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = initial.size();
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::static_data;
        ubo_descriptor.initial_data = initial.data();
        m_tonemap_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::texture});
        input_layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        gpu::bind_group_descriptor input_bind_group_descriptor{};
        input_bind_group_descriptor.layout = m_input_layout;

        gpu::binding_value scene_color_slot{};
        scene_color_slot.binding = 0;
        scene_color_slot.kind = gpu::binding_kind::texture;
        scene_color_slot.texture_value = input_color;
        input_bind_group_descriptor.entries.push_back(scene_color_slot);

        gpu::binding_value tonemap_slot{};
        tonemap_slot.binding = 1;
        tonemap_slot.kind = gpu::binding_kind::uniform_buffer;
        tonemap_slot.buffer_value = m_tonemap_ubo;
        input_bind_group_descriptor.entries.push_back(tonemap_slot);

        m_input_bind_group = gpu.create_bind_group(input_bind_group_descriptor);

        // Fullscreen triangle: depth disabled, blend disabled, no
        // culling so the triangle's winding is irrelevant. The
        // vertex shader reads a single vec2 attribute from
        // @ref m_vertex_buffer.
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

    tonemap_pass::~tonemap_pass()
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
        if (m_tonemap_ubo.valid())
        {
            gpu.destroy(m_tonemap_ubo);
            m_tonemap_ubo = {};
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

    void tonemap_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        gpu::render_pass_descriptor descriptor{};
        // Resolve into the off-screen LDR target rather than straight to
        // the swapchain: the final post effect (FXAA) needs to sample
        // this tonemapped result as a shader input, which the swapchain
        // cannot provide.
        descriptor.target = ctx.ldr_color_target;
        // The fullscreen triangle covers every pixel; clearing is
        // strictly redundant but cheap and keeps the target in a
        // known state if a future post pass narrows its viewport.
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

    void tonemap_pass::set_exposure(float exposure)
    {
        if (exposure == m_exposure)
        {
            return;
        }
        m_exposure = exposure;
        upload_uniforms();
    }

    void tonemap_pass::set_operator(tonemap_operator op)
    {
        if (op == m_operator)
        {
            return;
        }
        m_operator = op;
        upload_uniforms();
    }

    void tonemap_pass::upload_uniforms()
    {
        auto& gpu = *runtime::current_engine().gpu;
        const std::array<std::byte, tonemap_ubo_size> bytes = pack_uniforms(m_exposure, m_operator);
        gpu.write_buffer(m_tonemap_ubo, bytes.data(), bytes.size(), 0);
    }
} // namespace rendering_engine
