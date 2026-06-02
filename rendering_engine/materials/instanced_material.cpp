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
#include <string>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <runtime/engine.hpp>

namespace
{
    // Binding numbers. UBOs share a single namespace across every
    // descriptor set on the OpenGL backend (ARB_gl_spirv), so they must
    // stay globally unique: the scene_pass owns camera = 0 (and lights = 2)
    // in the per-frame set, leaving 3 for the per-material tint block. The
    // per-instance data lives in a storage buffer, whose binding namespace
    // is disjoint from the UBO one, so it can reuse binding 1.
    constexpr uint32_t instance_buffer_binding = rendering_engine::instanced_material::instance_buffer_binding;
    constexpr uint32_t material_params_binding = 3;

    // std140 layout for the per-material params UBO: a single vec4 tint,
    // 16 bytes.
    constexpr size_t material_ubo_size = 4 * sizeof(float);

    // The vertex shader reads the per-instance model matrix and tint from a
    // storage buffer indexed by gl_InstanceIndex. The std430 record is a
    // mat4 (64 bytes) followed by a vec4 (16 bytes); @ref instanced_mesh
    // uploads a matching POD array.
    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;

        layout(location = 0) out vec4 instanceColor;

        layout(set = 0, binding = 0, std140) uniform PerFrame
        {
            mat4 viewMatrix;
            mat4 projectionMatrix;
        } u_frame;

        struct instance_data
        {
            mat4 model;
            vec4 color;
        };

        layout(set = 1, binding = 1, std430) readonly buffer Instances
        {
            instance_data instances[];
        } u_instances;

        void main()
        {
            instance_data inst = u_instances.instances[gl_InstanceIndex];
            instanceColor = inst.color;
            mat4 MVP = u_frame.projectionMatrix * u_frame.viewMatrix * inst.model;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) in vec4 instanceColor;
        layout(location = 0) out vec4 fragColor;

        layout(set = 2, binding = 3, std140) uniform Material
        {
            vec4 color;
        } u_material;

        void main()
        {
            fragColor = u_material.color * instanceColor;
        }
)fs";
} // namespace

namespace rendering_engine
{
    instanced_material::instanced_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — the per-instance stride is supplied at
        // set_vertex_buffer time. Only the position is consumed; any
        // uv / normal channels in the shared geometry are ignored.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});

        // Per-draw layout (slot 1): the per-instance storage buffer at
        // binding 1, matching the bind group @ref instanced_mesh builds.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({instance_buffer_binding, gpu::binding_kind::storage_buffer});

        // Per-material layout (slot 2): the flat-tint UBO owned by this
        // material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({material_params_binding, gpu::binding_kind::uniform_buffer});

        // Opaque unlit surface: depth tested and written, no blending.
        material_params params{};
        params.transparent = false;
        params.depth_test = true;
        params.depth_write = true;

        construct_pipeline(
            vertex_shader, fragment_shader, vertex_layout, draw_layout, frame_layout, params, material_layout);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        auto& gpu = *runtime::current_engine().gpu;
        m_material_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_per_material_layout;
        gpu::binding_value ubo_slot{};
        ubo_slot.binding = material_params_binding;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);
        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    instanced_material::~instanced_material()
    {
        // Drop the bind group before the buffer it references, then null
        // it so the base destructor's destruct_pipeline does not double-free.
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t instanced_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void instanced_material::set_color(const util::color& color)
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

        auto& gpu = *runtime::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
