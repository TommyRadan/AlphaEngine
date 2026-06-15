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

#include <rendering_engine/passes/post/velocity_pass.hpp>

#include <string>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
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
    // Reconstructs each pixel's previous-frame screen position from depth.
    // The current pixel's clip-space point (NDC xy from the UV, NDC z from
    // the depth buffer — GL convention, z in [-1, 1]) is pushed through the
    // baked reprojection matrix prevViewProj * inverse(curViewProj), which
    // takes it to the previous frame's clip space directly; the perspective
    // divide there yields the previous NDC and hence the previous UV. The
    // motion vector is the UV delta, so the TAA resolve reads history at
    // texCoord - velocity.
    //
    // Background pixels (depth at the far plane) reproject through the same
    // matrix, so a panning or rotating camera still produces correct motion
    // for the skybox.
    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec2 texCoord;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 0) uniform sampler2D sceneDepth;

        layout(set = 0, binding = 1, std140) uniform Reprojection
        {
            mat4 reprojection; // prevViewProj * inverse(curViewProj), unjittered
        } u_reproj;

        void main()
        {
            float depth = texture(sceneDepth, texCoord).r;

            // Current pixel in NDC (clip space before the divide is the same
            // point scaled by w, which cancels in the divide below).
            vec3 ndc = vec3(texCoord * 2.0 - 1.0, depth * 2.0 - 1.0);

            vec4 prev_clip = u_reproj.reprojection * vec4(ndc, 1.0);
            vec2 prev_ndc = prev_clip.xy / prev_clip.w;
            vec2 prev_uv = prev_ndc * 0.5 + 0.5;

            fragColor = vec4(texCoord - prev_uv, 0.0, 0.0);
        }
)fs";

    // std140: a single mat4 occupies 64 bytes.
    constexpr size_t reproj_ubo_size = sizeof(core::math::mat4);
} // namespace

namespace rendering_engine
{
    velocity_pass::velocity_pass(gpu::texture scene_depth, uint32_t width, uint32_t height)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Degenerate backbuffer (no settings, zero-sized window): leave the
        // pass disabled so velocity_texture() reports invalid.
        if (width == 0 || height == 0)
        {
            return;
        }

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(fullscreen_triangle_vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_glsl_to_spirv(fragment_shader, gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        gpu::buffer_descriptor vb_descriptor{};
        vb_descriptor.size = fullscreen_triangle_vertices.size() * sizeof(float);
        vb_descriptor.usage = gpu::buffer_usage_vertex;
        vb_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vb_descriptor.initial_data = fullscreen_triangle_vertices.data();
        m_vertex_buffer = gpu.create_buffer(vb_descriptor);

        // The reprojection matrix is rewritten every frame from the live
        // camera, so the buffer is dynamic and copy-dst.
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = reproj_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_reproj_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_layout_descriptor layout{};
        layout.entries.push_back({0, gpu::binding_kind::texture});
        layout.entries.push_back({1, gpu::binding_kind::uniform_buffer});
        m_layout = gpu.create_bind_group_layout(layout);

        // Two-channel signed motion needs a float target; the engine has no
        // RG format, so rgba16f carries the vector in xy and leaves zw at 0.
        gpu::render_target_descriptor velocity_descriptor{};
        velocity_descriptor.color_format = gpu::texture_format::rgba16_float;
        velocity_descriptor.width = width;
        velocity_descriptor.height = height;
        velocity_descriptor.with_depth = false;
        m_velocity_target = gpu.create_render_target(velocity_descriptor);
        m_velocity_texture = gpu.render_target_color_texture(m_velocity_target);

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
        reproj_slot.buffer_value = m_reproj_ubo;
        bind_group_descriptor.entries.push_back(reproj_slot);

        m_bind_group = gpu.create_bind_group(bind_group_descriptor);

        m_enabled = true;
    }

    velocity_pass::~velocity_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        if (m_bind_group.valid())
        {
            gpu.destroy(m_bind_group);
            m_bind_group = {};
        }
        if (m_velocity_target.valid())
        {
            gpu.destroy(m_velocity_target);
            m_velocity_target = {};
            m_velocity_texture = {};
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
        if (m_reproj_ubo.valid())
        {
            gpu.destroy(m_reproj_ubo);
            m_reproj_ubo = {};
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

    gpu::texture velocity_pass::velocity_texture() const
    {
        return m_velocity_texture;
    }

    void velocity_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        if (!m_enabled)
        {
            return;
        }

        auto& gpu = *runtime::current_engine().gpu;

        // No camera: clear the motion to zero so the TAA resolve falls back
        // to same-pixel history, and forget the previous matrix so the next
        // camera frame starts fresh (zero motion) rather than reprojecting
        // across the gap.
        if (ctx.active_camera == nullptr)
        {
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_velocity_target;
            descriptor.color.load = gpu::load_op::clear;
            descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
            descriptor.use_depth = false;
            auto pass_encoder = encoder.begin_render_pass(descriptor);
            pass_encoder->end();
            m_has_prev = false;
            return;
        }

        // Build the current unjittered view-projection (the scene pass keeps
        // its sub-pixel jitter local, so the camera's matrices are clean).
        const core::math::mat4 view = ctx.active_camera->get_view_matrix();
        const core::math::mat4 projection = ctx.active_camera->get_projection_matrix();
        const core::math::mat4 view_proj = projection * view;

        // First camera frame has no history: reproject against this frame so
        // every pixel reports zero motion.
        const core::math::mat4& prev_view_proj = m_has_prev ? m_prev_view_proj : view_proj;
        const core::math::mat4 reprojection = prev_view_proj * core::math::inverse(view_proj);
        gpu.write_buffer(m_reproj_ubo, reprojection.data(), reproj_ubo_size, 0);

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_velocity_target;
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3, 0);
        pass_encoder->end();

        m_prev_view_proj = view_proj;
        m_has_prev = true;
    }
} // namespace rendering_engine
