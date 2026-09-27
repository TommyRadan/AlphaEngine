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

#include <rendering_engine/renderables/per_draw_ubo.hpp>

#include <cmath>

#include <core/math/mat3.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>

namespace
{
    // Below this the 3x3 is treated as singular; a scale axis this
    // small has no meaningful normal anyway.
    constexpr float singular_epsilon = 1e-12f;

    // Column-major upper-left 3x3 of a column-major 4x4.
    core::math::mat3 upper_left(const core::math::mat4& model)
    {
        core::math::mat3 m{};
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                m.m[column * 3 + row] = model.m[column * 4 + row];
            }
        }
        return m;
    }
} // namespace

namespace rendering_engine
{
    float model_determinant(const core::math::mat4& model)
    {
        const float* m = model.m;
        // m[col * 4 + row]; expand along the first column.
        const float a = m[0];
        const float b = m[1];
        const float c = m[2];
        const float d = m[4];
        const float e = m[5];
        const float f = m[6];
        const float g = m[8];
        const float h = m[9];
        const float i = m[10];
        return a * (e * i - f * h) - d * (b * i - c * h) + g * (b * f - c * e);
    }

    bool is_mirrored(const core::math::mat4& model)
    {
        return model_determinant(model) < 0.0f;
    }

    per_draw_payload make_per_draw_payload(const core::math::mat4& model)
    {
        per_draw_payload payload{};
        payload.model = model;

        core::math::mat3 normal = upper_left(model);
        if (std::fabs(model_determinant(model)) > singular_epsilon)
        {
            normal = core::math::transpose(core::math::inverse(normal));
        }
        // Widen to the identity-padded mat4 the block declares.
        payload.normal = core::math::mat4{};
        for (int column = 0; column < 3; ++column)
        {
            for (int row = 0; row < 3; ++row)
            {
                payload.normal.m[column * 4 + row] = normal.m[column * 3 + row];
            }
        }
        return payload;
    }

    gpu::bind_group_layout_entry per_draw_model_layout_entry()
    {
        gpu::bind_group_layout_entry entry{gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer};
        entry.has_dynamic_offset = true;
        return entry;
    }

    gpu::buffer create_per_draw_ubo(gpu::device& device)
    {
        gpu::buffer_descriptor descriptor{};
        descriptor.size = per_draw_ubo_size;
        descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        return device.create_buffer(descriptor);
    }

    gpu::bind_group create_per_draw_bind_group(gpu::device& device, gpu::bind_group_layout layout, gpu::buffer ubo)
    {
        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = layout;
        gpu::binding_value model_slot{};
        model_slot.binding = gpu::shader_bindings::per_draw_model;
        model_slot.kind = gpu::binding_kind::uniform_buffer;
        model_slot.buffer_value = ubo;
        model_slot.size = per_draw_ubo_size;
        descriptor.entries.push_back(model_slot);
        return device.create_bind_group(descriptor);
    }

    bool write_per_draw_ubo(gpu::device& device, gpu::buffer ubo, const core::math::mat4& model)
    {
        const per_draw_payload payload = make_per_draw_payload(model);
        device.write_buffer(ubo, &payload, per_draw_ubo_size, 0);
        return is_mirrored(model);
    }

    gpu::buffer create_joint_buffer(gpu::device& device, size_t joint_count)
    {
        gpu::buffer_descriptor descriptor{};
        descriptor.size = (joint_count > 0 ? joint_count : 1) * joint_matrix_size;
        descriptor.usage = gpu::buffer_usage_storage | gpu::buffer_usage_copy_dst;
        descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        return device.create_buffer(descriptor);
    }

    gpu::bind_group create_skinned_per_draw_bind_group(gpu::device& device,
                                                       gpu::bind_group_layout layout,
                                                       gpu::buffer ubo,
                                                       gpu::buffer joints)
    {
        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = layout;
        gpu::binding_value model_slot{};
        model_slot.binding = gpu::shader_bindings::per_draw_model;
        model_slot.kind = gpu::binding_kind::uniform_buffer;
        model_slot.buffer_value = ubo;
        descriptor.entries.push_back(model_slot);
        gpu::binding_value joints_slot{};
        joints_slot.binding = gpu::shader_bindings::per_draw_joints;
        joints_slot.kind = gpu::binding_kind::storage_buffer;
        joints_slot.buffer_value = joints;
        descriptor.entries.push_back(joints_slot);
        return device.create_bind_group(descriptor);
    }

    void write_joint_buffer(gpu::device& device, gpu::buffer joints, std::span<const core::math::mat4> matrices)
    {
        if (matrices.empty())
        {
            return;
        }
        device.write_buffer(joints, matrices.data(), matrices.size() * joint_matrix_size, 0);
    }
} // namespace rendering_engine
