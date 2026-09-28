// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/basic_material.hpp>

#include <array>
#include <utility>

#include <assets/vertex.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>

namespace
{
    // std140 layout for the per-material params UBO: vec4 color at
    // offset 0, float useTexture at offset 16. The struct rounds up to
    // 32 bytes.
    constexpr size_t material_ubo_size = 32;
} // namespace

namespace rendering_engine
{
    material_template_descriptor basic_material::describe()
    {
        material_template_descriptor descriptor{};
        descriptor.name = "basic";
        descriptor.vertex_shader = gpu::shader_variant{"materials/basic.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/basic.frag.glsl"};

        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are supplied at
        // set_vertex_buffer time. Position sits at offset 0 and UV at
        // offset 12 across every position+uv(+normal) vertex this
        // pipeline draws, so the baked attribute offsets stay valid.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = assets::vertex_format::position_uv;
        descriptor.vertex_format_without_tangents = assets::vertex_format::position_uv;

        // No per-draw bindings: the model + normal matrix are pushed
        // (per_draw_ubo.hpp), so the per-draw layout (slot 1) is empty.
        descriptor.frame = material_frame::scene;

        // Per-material layout (slot 2): the {color, useTexture} UBO plus
        // the albedo sampler, both owned by each instance.
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});

        descriptor.instance_factory = make_instance_factory<basic_material>();
        return descriptor;
    }

    basic_material::basic_material(std::shared_ptr<material_template> tmpl)
        // Opaque unlit surface: depth tested and written, no blending.
        : material(std::move(tmpl), material_params{})
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_material_ubo = device().create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    basic_material::~basic_material()
    {
        // Drop the bind group before the buffer / texture it references.
        release_per_material_bind_group();
        release_map(m_albedo);
        if (m_material_ubo.valid())
        {
            device().destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    void basic_material::set_color(const assets::color& color)
    {
        m_color = color;
        upload_params();
    }

    void basic_material::set_albedo(const assets::image& image, assets::color_space space)
    {
        release_map(m_albedo);
        m_albedo = upload_map(image, space);
        rebuild_bind_group();
    }

    void basic_material::clear_albedo()
    {
        if (!m_albedo.valid())
        {
            return;
        }
        release_map(m_albedo);
        rebuild_bind_group();
    }

    void basic_material::rebuild_bind_group()
    {
        release_per_material_bind_group();

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = get_template().per_material_layout();

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = gpu::shader_bindings::material_params;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        gpu::binding_value tex_slot{};
        tex_slot.binding = gpu::shader_bindings::material_albedo_map;
        tex_slot.kind = gpu::binding_kind::texture;
        tex_slot.texture_value = m_albedo;
        bg_descriptor.entries.push_back(tex_slot);

        m_per_material_bind_group = device().create_bind_group(bg_descriptor);

        upload_params();
    }

    void basic_material::upload_params()
    {
        std::array<float, 8> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;
        payload[4] = m_albedo.valid() ? 1.0f : 0.0f;

        device().write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
