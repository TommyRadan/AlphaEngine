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

#include <rendering_engine/passes/skybox_pass.hpp>

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
    // The fullscreen triangle's clip-space xy is reused as the screen
    // direction lookup. Emitting it at z = w pins every fragment to the
    // far plane so the depth test rejects the sky wherever scene geometry
    // already wrote a nearer depth.
    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec2 in_pos;
        layout(location = 0) out vec3 viewDir;

        layout(set = 0, binding = 0, std140) uniform Skybox
        {
            mat4 invViewProj;
        } u_sky;

        void main()
        {
            // Unproject the far-plane clip point back to world space. The
            // view matrix had its translation stripped, so the eye sits at
            // the origin and the unprojected point doubles as the ray.
            vec4 world = u_sky.invViewProj * vec4(in_pos, 1.0, 1.0);
            viewDir = world.xyz / world.w;
            gl_Position = vec4(in_pos, 1.0, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec3 viewDir;
        layout(location = 0) out vec4 fragColor;

        layout(set = 0, binding = 1) uniform samplerCube skybox;

        void main()
        {
            fragColor = vec4(texture(skybox, normalize(viewDir)).rgb, 1.0);
        }
)fs";

    // std140 size of the Skybox UBO: a single mat4.
    constexpr size_t sky_ubo_size = sizeof(core::math::mat4);
} // namespace

namespace rendering_engine
{
    skybox_pass::skybox_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(vertex_shader, gpu::shader_stage::vertex);
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

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sky_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_sky_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::uniform_buffer});
        input_layout.entries.push_back({1, gpu::binding_kind::texture});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        rebuild_bind_group();

        // Sky behind everything: depth tested against the loaded scene
        // depth with less-or-equal so the far-plane fragments pass where
        // the scene left the cleared 1.0, but never write depth. No
        // culling — the oversized triangle's winding is irrelevant.
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(float) * 2;
        vertex_layout.attributes.push_back({0, 2, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = true;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::less_equal;

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

    skybox_pass::~skybox_pass()
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
        if (m_sky_ubo.valid())
        {
            gpu.destroy(m_sky_ubo);
            m_sky_ubo = {};
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

    void skybox_pass::set_cubemap(gpu::texture cubemap)
    {
        m_cubemap = cubemap;
        rebuild_bind_group();
    }

    void skybox_pass::rebuild_bind_group()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_input_bind_group.valid())
        {
            gpu.destroy(m_input_bind_group);
            m_input_bind_group = {};
        }

        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_input_layout;

        gpu::binding_value sky_slot{};
        sky_slot.binding = 0;
        sky_slot.kind = gpu::binding_kind::uniform_buffer;
        sky_slot.buffer_value = m_sky_ubo;
        descriptor.entries.push_back(sky_slot);

        gpu::binding_value cube_slot{};
        cube_slot.binding = 1;
        cube_slot.kind = gpu::binding_kind::texture;
        cube_slot.texture_value = m_cubemap;
        descriptor.entries.push_back(cube_slot);

        m_input_bind_group = gpu.create_bind_group(descriptor);
    }

    void skybox_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // Dormant until a cube map is supplied, and a no-camera frame has
        // no view ray to reconstruct — leave the scene colour untouched.
        if (!m_cubemap.valid() || ctx.active_camera == nullptr)
        {
            return;
        }

        auto& gpu = *runtime::current_engine().gpu;

        // Strip the translation from the view matrix so the sky rotates
        // with the camera but never translates, then invert
        // projection * view so the vertex shader can unproject screen
        // corners into world-space ray directions.
        core::math::mat4 view = ctx.active_camera->get_view_matrix();
        view.data()[12] = 0.0f;
        view.data()[13] = 0.0f;
        view.data()[14] = 0.0f;
        const core::math::mat4 projection = ctx.active_camera->get_projection_matrix();
        const core::math::mat4 inv_view_proj = core::math::inverse(projection * view);
        gpu.write_buffer(m_sky_ubo, inv_view_proj.data(), sky_ubo_size, 0);

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.scene_color_target;
        // Load the scene pass's colour and depth: the sky composites behind
        // the geometry it already drew rather than wiping it.
        descriptor.color.load = gpu::load_op::load;
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::load;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->set_vertex_buffer(0, m_vertex_buffer, 0, 0);
        pass_encoder->draw(3, 0);
        pass_encoder->end();
    }
} // namespace rendering_engine
