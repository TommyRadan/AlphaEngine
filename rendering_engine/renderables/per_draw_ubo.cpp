// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/per_draw_ubo.hpp>

#include <cmath>

#include <core/math/mat3.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

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

    gpu::push_constant_range per_draw_push_constant_range()
    {
        gpu::push_constant_range range{};
        range.stages = gpu::shader_stages_vertex | gpu::shader_stages_fragment;
        range.offset = 0;
        range.size = static_cast<uint32_t>(per_draw_ubo_size);
        return range;
    }

    void push_per_draw(gpu::render_pass_encoder& encoder, const draw_item& item)
    {
        if (item.per_draw_push != nullptr)
        {
            const gpu::push_constant_range range = per_draw_push_constant_range();
            encoder.push_constants(range.stages, range.offset, range.size, item.per_draw_push);
        }
    }

    void bind_per_draw(gpu::render_pass_encoder& encoder, const draw_item& item, uint32_t slot)
    {
        push_per_draw(encoder, item);
        if (item.per_draw_bind_group.valid())
        {
            encoder.set_bind_group(slot, item.per_draw_bind_group);
        }
    }

    gpu::buffer create_joint_buffer(gpu::device& device, size_t joint_count)
    {
        gpu::buffer_descriptor descriptor{};
        descriptor.size = (joint_count > 0 ? joint_count : 1) * joint_matrix_size;
        descriptor.usage = gpu::buffer_usage_storage | gpu::buffer_usage_copy_dst;
        descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        return device.create_buffer(descriptor);
    }

    gpu::bind_group
    create_skinned_per_draw_bind_group(gpu::device& device, gpu::bind_group_layout layout, gpu::buffer joints)
    {
        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = layout;
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

    void per_draw_binding::bind(const core::transform& transform, draw_item& item)
    {
        const uint64_t version = transform.get_world_version();
        if (version != m_world_version)
        {
            m_payload = make_per_draw_payload(transform.get_world_matrix());
            m_mirrored = is_mirrored(m_payload.model);
            m_world_version = version;
        }
        // The pass pushes the cached block right before the draw; the
        // bytes are copied into the command stream there, so a draw an
        // earlier pass recorded keeps the block it pushed.
        item.per_draw_push = &m_payload;
        item.mirrored = m_mirrored;
    }
} // namespace rendering_engine
