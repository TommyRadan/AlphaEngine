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

#include <rendering_engine/materials/instanced_material.hpp>

#include <array>
#include <utility>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>

namespace
{
    // std140 layout for the per-material params UBO: a single vec4 tint,
    // 16 bytes.
    constexpr size_t material_ubo_size = 4 * sizeof(float);

    // Per-instance vertex attribute locations. Location 0 is the shared
    // geometry position; the model matrix occupies four consecutive vec4
    // slots (one per column) and the tint follows. The vertex shader
    // reads them as ordinary attributes from a per-instance stream
    // (divisor 1), so the path does not depend on gl_InstanceIndex
    // behaving across the SPIR-V -> GL translation.
    constexpr uint32_t position_location = 0;
    constexpr uint32_t model_column0_location = 1;
    constexpr uint32_t instance_color_location = 5;
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template> instanced_material::create_template(gpu::device& device,
                                                                           gpu::bind_group_layout frame_layout)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "instanced";
        descriptor.vertex_shader = gpu::shader_variant{"materials/instanced.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/instanced.frag.glsl"};

        // Slot 0: the shared geometry stream. Stride 0 — the per-renderable
        // stride is supplied at set_vertex_buffer time. Only the position is
        // consumed; any uv / normal channels in the geometry are ignored.
        gpu::vertex_buffer_layout geometry_layout{};
        geometry_layout.stride = 0;
        geometry_layout.step_mode = gpu::vertex_step_mode::vertex;
        geometry_layout.attributes.push_back({position_location, 3, gpu::scalar_type::float32, 0});
        descriptor.required_vertex_format = vertex_format::position;
        descriptor.vertex_format_without_tangents = vertex_format::position;

        // Slot 1: the per-instance stream (divisor 1). A mat4 model as four
        // vec4 columns followed by a vec4 tint, matching the record
        // @ref instanced_mesh uploads.
        gpu::vertex_buffer_layout instance_layout{};
        instance_layout.stride = instance_buffer_stride;
        instance_layout.step_mode = gpu::vertex_step_mode::instance;
        const auto vec4_size = static_cast<uint32_t>(4 * sizeof(float));
        for (uint32_t column = 0; column < 4; ++column)
        {
            instance_layout.attributes.push_back(
                {model_column0_location + column, 4, gpu::scalar_type::float32, column * vec4_size});
        }
        instance_layout.attributes.push_back({instance_color_location, 4, gpu::scalar_type::float32, 4 * vec4_size});
        descriptor.vertex_layouts = {geometry_layout, instance_layout};

        // No per-draw bind group: the per-instance data is a vertex stream.
        descriptor.frame_layout = frame_layout;

        // Per-material layout (slot 2): the flat-tint UBO owned by each
        // instance.
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});

        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    instanced_material::instanced_material(std::shared_ptr<material_template> tmpl)
        // Opaque unlit surface: depth tested and written, no blending.
        : material(std::move(tmpl), material_params{})
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_material_ubo = device().create_buffer(ubo_descriptor);

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = get_template().per_material_layout();
        gpu::binding_value ubo_slot{};
        ubo_slot.binding = gpu::shader_bindings::material_params;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);
        m_per_material_bind_group = device().create_bind_group(bg_descriptor);

        upload_params();
    }

    instanced_material::~instanced_material()
    {
        // Drop the bind group before the buffer it references.
        release_per_material_bind_group();
        if (m_material_ubo.valid())
        {
            device().destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    void instanced_material::set_color(const color& color)
    {
        m_color = color;
        upload_params();
    }

    void instanced_material::upload_params()
    {
        std::array<float, 4> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;

        device().write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
