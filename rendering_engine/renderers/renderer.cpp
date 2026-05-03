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

#include <rendering_engine/renderers/renderer.hpp>

#include <control/engine.hpp>
#include <rendering_engine/gpu/device.hpp>

namespace rendering_engine::renderers
{
    renderer::~renderer()
    {
        destruct_pipeline();
    }

    gpu::pipeline renderer::pipeline_handle() const
    {
        return m_pipeline;
    }

    gpu::bind_group_layout renderer::draw_bind_group_layout() const
    {
        return m_draw_layout;
    }

    uint32_t renderer::draw_bind_group_slot() const
    {
        return m_frame_layout.valid() ? 1u : 0u;
    }

    void renderer::begin(gpu::render_pass_encoder& encoder)
    {
        encoder.set_pipeline(m_pipeline);
    }

    void renderer::end(gpu::render_pass_encoder& /*encoder*/) {}

    void renderer::construct_pipeline(const std::string& vertex_source,
                                      const std::string& fragment_source,
                                      const gpu::vertex_buffer_layout& vertex_layout,
                                      const gpu::bind_group_layout_descriptor& draw_layout,
                                      const gpu::bind_group_layout_descriptor* frame_layout,
                                      const gpu::depth_state& depth,
                                      const gpu::blend_state& blend,
                                      const gpu::rasterizer_state& rasterizer)
    {
        auto& gpu = *control::current_engine().gpu;

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.source = vertex_source;
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.source = fragment_source;
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        m_draw_layout = gpu.create_bind_group_layout(draw_layout);
        if (frame_layout != nullptr)
        {
            m_frame_layout = gpu.create_bind_group_layout(*frame_layout);
        }

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.fragment_shader = m_fragment_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        if (m_frame_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_frame_layout);
        }
        pipeline_descriptor.bind_group_layouts.push_back(m_draw_layout);

        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    void renderer::destruct_pipeline()
    {
        // Renderers are destroyed by @ref rendering_engine::context::quit
        // before the device is shut down, so the device is always
        // live here. If callers ever construct/destruct renderers
        // outside that lifecycle they must still arrange for the
        // device to outlive the renderer.
        auto& gpu = *control::current_engine().gpu;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_draw_layout.valid())
        {
            gpu.destroy(m_draw_layout);
            m_draw_layout = {};
        }
        if (m_frame_layout.valid())
        {
            gpu.destroy(m_frame_layout);
            m_frame_layout = {};
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
} // namespace rendering_engine::renderers
