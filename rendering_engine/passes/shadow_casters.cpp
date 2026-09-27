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

#include <rendering_engine/passes/shadow_casters.hpp>

#include <string>

#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Vertex attribute locations shared with shaders/passes/shadow_instanced.vert.glsl:
    // the geometry position at 0, then the four model-matrix columns of the
    // per-instance record. The record's trailing tint is not declared —
    // the depth-only stage never reads it.
    constexpr uint32_t position_location = 0;
    constexpr uint32_t model_column0_location = 1;
} // namespace

namespace rendering_engine
{
    instanced_shadow_pipeline create_instanced_shadow_pipeline(gpu::bind_group_layout light_layout,
                                                               const gpu::depth_state& depth,
                                                               const gpu::blend_state& blend,
                                                               const gpu::rasterizer_state& rasterizer,
                                                               const gpu::depth_bias_state& depth_bias)
    {
        auto& gpu = *runtime::current_engine().gpu;
        instanced_shadow_pipeline instanced{};

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv =
            gpu::compile_library_shader("passes/shadow_instanced.vert.glsl", gpu::shader_stage::vertex);
        instanced.vertex_shader = gpu.create_shader_module(vs_descriptor);

        // Slot 0: the shared geometry, position only, stride supplied per
        // draw (the renderables' records differ in width).
        gpu::vertex_buffer_layout geometry_layout{};
        geometry_layout.stride = 0;
        geometry_layout.step_mode = gpu::vertex_step_mode::vertex;
        geometry_layout.attributes.push_back({position_location, 3, gpu::scalar_type::float32, 0});

        // Slot 1: the per-instance stream instanced_mesh uploads, laid out
        // exactly as instanced_material declares it — a mat4 as four vec4
        // columns at the front of each record.
        gpu::vertex_buffer_layout instance_layout{};
        instance_layout.stride = instanced_material::instance_buffer_stride;
        instance_layout.step_mode = gpu::vertex_step_mode::instance;
        const auto vec4_size = static_cast<uint32_t>(4 * sizeof(float));
        for (uint32_t column = 0; column < 4; ++column)
        {
            instance_layout.attributes.push_back(
                {model_column0_location + column, 4, gpu::scalar_type::float32, column * vec4_size});
        }

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = instanced.vertex_shader;
        pipeline_descriptor.vertex_buffers.push_back(geometry_layout);
        pipeline_descriptor.vertex_buffers.push_back(instance_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.depth_bias = depth_bias;
        // Only the light group: the model matrices come from the vertex
        // stream, so there is no per-draw set.
        pipeline_descriptor.bind_group_layouts.push_back(light_layout);
        instanced.pipeline = gpu.create_pipeline(pipeline_descriptor);

        return instanced;
    }

    void destroy_instanced_shadow_pipeline(instanced_shadow_pipeline& instanced)
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (instanced.pipeline.valid())
        {
            gpu.destroy(instanced.pipeline);
            instanced.pipeline = {};
        }
        if (instanced.vertex_shader.valid())
        {
            gpu.destroy(instanced.vertex_shader);
            instanced.vertex_shader = {};
        }
    }

    shadow_caster_dispatch::shadow_caster_dispatch(gpu::render_pass_encoder& encoder,
                                                   gpu::pipeline single_pipeline,
                                                   gpu::pipeline instanced_pipeline,
                                                   gpu::bind_group light_bind_group)
        : m_encoder(encoder), m_single_pipeline(single_pipeline), m_instanced_pipeline(instanced_pipeline),
          m_light_bind_group(light_bind_group)
    {
    }

    void shadow_caster_dispatch::draw(const draw_item& item)
    {
        const bool instanced = item.indirect_buffer.valid();
        if (instanced ? (!item.instance_buffer.valid() || !item.index_buffer.valid())
                      : !item.per_draw_bind_group.valid())
        {
            // No model matrix to place the caster with: nothing sensible
            // could be rasterized into the map.
            return;
        }

        // Switch pipelines only when the item kind changes; the light group
        // is re-bound with it since both pipelines take it at slot 0.
        const gpu::pipeline wanted = instanced ? m_instanced_pipeline : m_single_pipeline;
        if (wanted != m_bound_pipeline)
        {
            m_encoder.set_pipeline(wanted);
            m_encoder.set_bind_group(0, m_light_bind_group);
            m_bound_pipeline = wanted;
        }

        m_encoder.set_vertex_buffer(0, item.vertex_buffer, 0, item.vertex_stride);
        if (instanced)
        {
            // The instance stream at vertex slot 1 carries the transforms;
            // the indirect record carries the index and instance counts.
            m_encoder.set_vertex_buffer(1, item.instance_buffer, 0, item.instance_stride);
            m_encoder.set_index_buffer(item.index_buffer, item.index_format);
            m_encoder.draw_indexed_indirect(item.indirect_buffer, 0);
            return;
        }

        m_encoder.set_bind_group(1, item.per_draw_bind_group);
        if (item.index_buffer.valid())
        {
            m_encoder.set_index_buffer(item.index_buffer, item.index_format);
            m_encoder.draw_indexed(item.index_count, 0);
        }
        else
        {
            m_encoder.draw(item.vertex_count, 0);
        }
    }
} // namespace rendering_engine
