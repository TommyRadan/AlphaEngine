// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/points_material.hpp>

#include <array>
#include <utility>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>

namespace
{
    // std140 layout for the per-material params UBO: vec4 color at
    // offset 0, then float size, sizeAttenuation, useTexture at
    // offsets 16/20/24. The struct rounds up to 32 bytes.
    constexpr size_t material_ubo_size = 32;
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template> points_material::create_template(gpu::device& device,
                                                                        gpu::bind_group_layout frame_layout)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "points";
        // The sprite map takes the shared albedo-map binding.
        descriptor.vertex_shader = gpu::shader_variant{"materials/points.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/points.frag.glsl"};

        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — the per-point stride is supplied at
        // set_vertex_buffer time by the renderable. Position sits at
        // offset 0 and the per-point colour at offset 12.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 3, gpu::scalar_type::float32, sizeof(float) * 3});
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = assets::vertex_format::position_color;
        descriptor.vertex_format_without_tangents = assets::vertex_format::position_color;

        // No per-draw bindings: the model + normal matrix are pushed
        // (per_draw_ubo.hpp), so the per-draw layout (slot 1) is empty.
        descriptor.frame_layout = frame_layout;

        // Per-material layout (slot 2): the params UBO plus the sprite
        // sampler, both owned by each instance.
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});

        descriptor.topology = gpu::primitive_topology::points;
        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    points_material::points_material(std::shared_ptr<material_template> tmpl)
        // Opaque unlit sprites: depth tested and written, no blending.
        // Particle-style additive blends are opt-in through
        // set_transparent / set_blending.
        : material(std::move(tmpl), material_params{})
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_material_ubo = device().create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    points_material::~points_material()
    {
        // Drop the bind group before the buffer / texture it references.
        release_per_material_bind_group();
        release_map(m_sprite);
        if (m_material_ubo.valid())
        {
            device().destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    void points_material::set_color(const assets::color& color)
    {
        m_color = color;
        upload_params();
    }

    void points_material::set_size(float size)
    {
        m_size = size;
        upload_params();
    }

    void points_material::set_size_attenuation(bool enabled)
    {
        m_size_attenuation = enabled;
        upload_params();
    }

    void points_material::set_sprite(const assets::image& image, assets::color_space space)
    {
        release_map(m_sprite);
        m_sprite = upload_map(image, space, gpu::address_mode::clamp_edge);
        rebuild_bind_group();
    }

    void points_material::clear_sprite()
    {
        if (!m_sprite.valid())
        {
            return;
        }
        release_map(m_sprite);
        rebuild_bind_group();
    }

    void points_material::rebuild_bind_group()
    {
        release_per_material_bind_group();

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = get_template().per_material_layout();

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = gpu::shader_bindings::material_params;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        gpu::binding_value sprite_slot{};
        sprite_slot.binding = gpu::shader_bindings::material_albedo_map;
        sprite_slot.kind = gpu::binding_kind::texture;
        sprite_slot.texture_value = m_sprite;
        bg_descriptor.entries.push_back(sprite_slot);

        m_per_material_bind_group = device().create_bind_group(bg_descriptor);

        upload_params();
    }

    void points_material::upload_params()
    {
        std::array<float, 8> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;
        payload[4] = m_size;
        payload[5] = m_size_attenuation ? 1.0f : 0.0f;
        payload[6] = m_sprite.valid() ? 1.0f : 0.0f;

        device().write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
