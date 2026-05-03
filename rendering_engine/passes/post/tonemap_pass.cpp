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

#include <string>

#include <control/engine.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/passes/post/fullscreen_triangle.hpp>

namespace
{
    // Krzysztof Narkowicz's ACES filmic approximation with an
    // explicit gamma-2.2 encode. The swapchain is regular
    // GL_RGBA8 (no SDL sRGB attribute, no GL_FRAMEBUFFER_SRGB),
    // so the framebuffer is treated as linear and the encode has
    // to happen here. Gamma 2.2 is visually indistinguishable
    // from the piecewise sRGB curve at these magnitudes and is
    // the standard chain partner for ACES.
    const std::string fragment_shader = R"fs(
        #version 330

        in vec2 texCoord;
        out vec4 fragColor;

        uniform sampler2D sceneColor;
        uniform float exposure;

        vec3 aces_filmic(vec3 x)
        {
            const float a = 2.51;
            const float b = 0.03;
            const float c = 2.43;
            const float d = 0.59;
            const float e = 0.14;
            return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
        }

        void main()
        {
            vec3 hdr = texture(sceneColor, texCoord).rgb * exposure;
            vec3 ldr = aces_filmic(hdr);
            vec3 srgb = pow(ldr, vec3(1.0 / 2.2));
            fragColor = vec4(srgb, 1.0);
        }
)fs";
} // namespace

namespace rendering_engine
{
    tonemap_pass::tonemap_pass(gpu::texture input_color)
    {
        auto& gpu = *control::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.source = fullscreen_triangle_vertex_shader;
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.source = fragment_shader;
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        gpu::bind_group_layout_descriptor input_layout{};
        input_layout.entries.push_back({0, gpu::binding_kind::texture, "sceneColor"});
        input_layout.entries.push_back({1, gpu::binding_kind::float_value, "exposure"});
        m_input_layout = gpu.create_bind_group_layout(input_layout);

        gpu::bind_group_descriptor input_bind_group_descriptor{};
        input_bind_group_descriptor.layout = m_input_layout;

        gpu::binding_value scene_color_slot{};
        scene_color_slot.binding = 0;
        scene_color_slot.kind = gpu::binding_kind::texture;
        scene_color_slot.texture_value = input_color;
        input_bind_group_descriptor.entries.push_back(scene_color_slot);

        // Captured once at construction; live tuning is out of
        // scope per the issue. A future auto-exposure pass will
        // update this slot per-frame via update_bind_group.
        gpu::binding_value exposure_slot{};
        exposure_slot.binding = 1;
        exposure_slot.kind = gpu::binding_kind::float_value;
        exposure_slot.float_value = 1.0f;
        input_bind_group_descriptor.entries.push_back(exposure_slot);

        m_input_bind_group = gpu.create_bind_group(input_bind_group_descriptor);

        // Fullscreen triangle: depth disabled, blend disabled, no
        // culling so the triangle's winding is irrelevant. The
        // vertex shader emits positions from gl_VertexID, so the
        // pipeline declares no vertex buffer layout.
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
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.bind_group_layouts.push_back(m_input_layout);

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    tonemap_pass::~tonemap_pass()
    {
        auto& gpu = *control::current_engine().gpu;
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
        descriptor.target = ctx.swapchain_target;
        // The fullscreen triangle covers every pixel; clearing is
        // strictly redundant but cheap and keeps the swapchain in a
        // known state if a future post pass narrows its viewport.
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_input_bind_group);
        pass_encoder->draw(3, 0);
        pass_encoder->end();
    }
} // namespace rendering_engine
