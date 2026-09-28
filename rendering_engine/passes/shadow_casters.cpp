// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/shadow_casters.hpp>

#include <string>

#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

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
    instanced_shadow_pipeline create_instanced_shadow_pipeline(gpu::device& device,
                                                               gpu::bind_group_layout light_layout,
                                                               const gpu::depth_state& depth,
                                                               const gpu::blend_state& blend,
                                                               const gpu::rasterizer_state& rasterizer,
                                                               const gpu::depth_bias_state& depth_bias)
    {
        instanced_shadow_pipeline instanced{};

        instanced.vertex_shader =
            gpu::create_library_shader_module(device, "passes/shadow_instanced.vert.glsl", gpu::shader_stage::vertex);

        // Slot 0: the shared geometry, position only, stride supplied per
        // draw (the renderables' records differ in width).
        gpu::vertex_buffer_layout geometry_layout{};
        geometry_layout.stride = 0;
        geometry_layout.step_mode = gpu::vertex_step_mode::vertex;
        geometry_layout.attributes.push_back({position_location, 3, gpu::scalar_type::float32, 0});

        // Slot 1: the per-instance stream of mesh_instance records, laid
        // out exactly as instanced_material declares it — a mat4 as four
        // vec4 columns at the front of each record.
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
        instanced.pipeline = device.create_pipeline(pipeline_descriptor);

        return instanced;
    }

    void destroy_instanced_shadow_pipeline(gpu::device& device, instanced_shadow_pipeline& instanced)
    {
        if (instanced.pipeline.valid())
        {
            device.destroy(instanced.pipeline);
            instanced.pipeline = {};
        }
        if (instanced.vertex_shader.valid())
        {
            device.destroy(instanced.vertex_shader);
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
        // An item with a per-instance stream carries its transforms there;
        // any other one needs its PerDraw block.
        const bool instanced = item.instance_buffer.valid();
        if (!instanced && item.per_draw_push == nullptr)
        {
            // No model matrix to place the caster with: nothing sensible
            // could be rasterized into the map.
            return;
        }
        if (item.indirect_buffer.valid() && !item.index_buffer.valid())
        {
            // Only the indexed path is drawn indirect.
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
            // The instance stream at vertex slot 1 carries the transforms.
            m_encoder.set_vertex_buffer(1, item.instance_buffer, 0, item.instance_stride);
        }
        else
        {
            // The same block the scene pass pushes.
            push_per_draw(m_encoder, item);
        }

        if (item.index_buffer.valid())
        {
            m_encoder.set_index_buffer(item.index_buffer, item.index_format);
            if (item.indirect_buffer.valid())
            {
                // The indirect record carries the index and instance counts.
                m_encoder.draw_indexed_indirect(item.indirect_buffer, 0);
            }
            else
            {
                m_encoder.draw_indexed(item.index_count, item.instance_count, item.first_index, item.vertex_offset);
            }
        }
        else
        {
            m_encoder.draw(item.vertex_count, item.instance_count, static_cast<uint32_t>(item.vertex_offset));
        }
    }
} // namespace rendering_engine
