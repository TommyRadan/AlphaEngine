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

#include <rendering_engine/materials/material.hpp>

#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <runtime/engine.hpp>

namespace
{
    using rendering_engine::blend_mode;
    using rendering_engine::material_params;
    namespace gpu = rendering_engine::gpu;

    gpu::depth_state to_depth_state(const material_params& params)
    {
        gpu::depth_state depth{};
        depth.test_enabled = params.depth_test;
        depth.write_enabled = params.depth_write;
        depth.compare = params.depth_test ? gpu::compare_function::less : gpu::compare_function::always;
        return depth;
    }

    gpu::blend_state to_blend_state(const material_params& params)
    {
        gpu::blend_state blend{};
        blend.enabled = params.transparent && params.blending != blend_mode::none;
        if (!blend.enabled)
        {
            return blend;
        }

        blend.op = gpu::blend_op::add;
        switch (params.blending)
        {
        case blend_mode::additive:
            blend.src = gpu::blend_factor::src_alpha;
            blend.dst = gpu::blend_factor::one;
            break;
        case blend_mode::subtractive:
            blend.src = gpu::blend_factor::zero;
            blend.dst = gpu::blend_factor::one_minus_src_color;
            break;
        case blend_mode::multiply:
            blend.src = gpu::blend_factor::zero;
            blend.dst = gpu::blend_factor::src_color;
            break;
        case blend_mode::normal:
        case blend_mode::none:
            blend.src = gpu::blend_factor::src_alpha;
            blend.dst = gpu::blend_factor::one_minus_src_alpha;
            break;
        }
        return blend;
    }

    gpu::rasterizer_state to_rasterizer_state(const material_params& params)
    {
        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = params.double_sided ? gpu::cull_mode::none : gpu::cull_mode::back;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = params.wireframe ? gpu::polygon_mode::line : gpu::polygon_mode::fill;
        return rasterizer;
    }
} // namespace

namespace rendering_engine
{
    material::~material()
    {
        destruct_pipeline();
    }

    gpu::pipeline material::pipeline() const
    {
        return m_pipeline;
    }

    const material_params& material::params() const
    {
        return m_params;
    }

    gpu::bind_group_layout material::per_draw_layout() const
    {
        return m_per_draw_layout;
    }

    uint32_t material::per_draw_slot() const
    {
        return m_has_frame_layout ? 1u : 0u;
    }

    gpu::bind_group material::per_material_bind_group() const
    {
        return m_per_material_bind_group;
    }

    uint32_t material::per_material_slot() const
    {
        return per_draw_slot();
    }

    void material::construct_pipeline(const std::string& vertex_source,
                                      const std::string& fragment_source,
                                      const gpu::vertex_buffer_layout& vertex_layout,
                                      const gpu::bind_group_layout_descriptor& draw_layout,
                                      gpu::bind_group_layout frame_layout,
                                      const material_params& params,
                                      const gpu::bind_group_layout_descriptor& material_layout,
                                      gpu::primitive_topology topology)
    {
        construct_pipeline(vertex_source,
                           fragment_source,
                           std::vector<gpu::vertex_buffer_layout>{vertex_layout},
                           draw_layout,
                           frame_layout,
                           params,
                           material_layout,
                           topology);
    }

    void material::construct_pipeline(const std::string& vertex_source,
                                      const std::string& fragment_source,
                                      const std::vector<gpu::vertex_buffer_layout>& vertex_layouts,
                                      const gpu::bind_group_layout_descriptor& draw_layout,
                                      gpu::bind_group_layout frame_layout,
                                      const material_params& params,
                                      const gpu::bind_group_layout_descriptor& material_layout,
                                      gpu::primitive_topology topology)
    {
        m_params = params;
        construct_pipeline(vertex_source,
                           fragment_source,
                           vertex_layouts,
                           draw_layout,
                           frame_layout,
                           to_depth_state(params),
                           to_blend_state(params),
                           to_rasterizer_state(params),
                           material_layout,
                           topology);
    }

    void material::construct_pipeline(const std::string& vertex_source,
                                      const std::string& fragment_source,
                                      const gpu::vertex_buffer_layout& vertex_layout,
                                      const gpu::bind_group_layout_descriptor& draw_layout,
                                      gpu::bind_group_layout frame_layout,
                                      const gpu::depth_state& depth,
                                      const gpu::blend_state& blend,
                                      const gpu::rasterizer_state& rasterizer,
                                      const gpu::bind_group_layout_descriptor& material_layout,
                                      gpu::primitive_topology topology)
    {
        construct_pipeline(vertex_source,
                           fragment_source,
                           std::vector<gpu::vertex_buffer_layout>{vertex_layout},
                           draw_layout,
                           frame_layout,
                           depth,
                           blend,
                           rasterizer,
                           material_layout,
                           topology);
    }

    void material::construct_pipeline(const std::string& vertex_source,
                                      const std::string& fragment_source,
                                      const std::vector<gpu::vertex_buffer_layout>& vertex_layouts,
                                      const gpu::bind_group_layout_descriptor& draw_layout,
                                      gpu::bind_group_layout frame_layout,
                                      const gpu::depth_state& depth,
                                      const gpu::blend_state& blend,
                                      const gpu::rasterizer_state& rasterizer,
                                      const gpu::bind_group_layout_descriptor& material_layout,
                                      gpu::primitive_topology topology)
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(vertex_source, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_glsl_to_spirv(fragment_source, gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        m_per_draw_layout = gpu.create_bind_group_layout(draw_layout);
        m_has_frame_layout = frame_layout.valid();

        // The trailing per-material descriptor set is optional; only
        // create it when the subclass asked for one. Its set index is
        // whatever comes after the per-frame (if any) and per-draw
        // layouts, so subclasses must report that slot via
        // @ref per_material_slot.
        if (!material_layout.entries.empty())
        {
            m_per_material_layout = gpu.create_bind_group_layout(material_layout);
        }

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.fragment_shader = m_fragment_shader;
        pipeline_descriptor.vertex_buffers = vertex_layouts;
        pipeline_descriptor.topology = topology;
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        if (m_has_frame_layout)
        {
            pipeline_descriptor.bind_group_layouts.push_back(frame_layout);
        }
        pipeline_descriptor.bind_group_layouts.push_back(m_per_draw_layout);
        if (m_per_material_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_per_material_layout);
        }

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    void material::destruct_pipeline()
    {
        // Materials are released in @ref rendering_engine::context::quit
        // before the device tears its pools down, so the device is live
        // here. Callers that construct/destruct materials outside that
        // lifecycle must arrange the same ordering.
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_per_material_layout.valid())
        {
            gpu.destroy(m_per_material_layout);
            m_per_material_layout = {};
        }
        if (m_per_draw_layout.valid())
        {
            gpu.destroy(m_per_draw_layout);
            m_per_draw_layout = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
        if (m_fragment_shader.valid())
        {
            gpu.destroy(m_fragment_shader);
            m_fragment_shader = {};
        }
    }
} // namespace rendering_engine
