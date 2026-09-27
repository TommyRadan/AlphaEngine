// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/line_material.hpp>

#include <array>
#include <utility>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>

namespace
{
    // std140 layout for the per-material params UBO: a single vec4 color
    // at offset 0. The block is 16 bytes.
    constexpr size_t material_ubo_size = 16;

    // Opaque unlit lines: depth tested and written by default, no
    // blending. The debug-overlay instance disables both so its gizmos
    // always draw on top of a depth-less pass.
    rendering_engine::material_params line_params(bool depth_tested)
    {
        rendering_engine::material_params params{};
        params.transparent = false;
        params.depth_test = depth_tested;
        params.depth_write = depth_tested;
        return params;
    }
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template> line_material::create_template(gpu::device& device,
                                                                      gpu::bind_group_layout frame_layout)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "line";
        descriptor.vertex_shader = gpu::shader_variant{"materials/line.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/line.frag.glsl"};

        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — the per-vertex stride is supplied at
        // set_vertex_buffer time by the renderable. Position sits at
        // offset 0 and the per-vertex colour at offset 12.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 3, gpu::scalar_type::float32, sizeof(float) * 3});
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = assets::vertex_format::position_color;
        descriptor.vertex_format_without_tangents = assets::vertex_format::position_color;

        // No per-draw bindings: the model + normal matrix are pushed
        // (per_draw_ubo.hpp), so the per-draw layout (slot 1) is empty.
        descriptor.frame_layout = frame_layout;

        // Per-material layout (slot 2): the tint params UBO owned by each
        // instance.
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});

        descriptor.topology = gpu::primitive_topology::lines;
        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    line_material::line_material(std::shared_ptr<material_template> tmpl, bool depth_tested)
        : material(std::move(tmpl), line_params(depth_tested))
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_material_ubo = device().create_buffer(ubo_descriptor);

        // The per-material bind group never changes shape (one UBO, no
        // textures), so build it once here rather than rebuilding on
        // every parameter change.
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

    line_material::~line_material()
    {
        // Drop the bind group before the buffer it references.
        release_per_material_bind_group();
        if (m_material_ubo.valid())
        {
            device().destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    void line_material::set_color(const assets::color& color)
    {
        m_color = color;
        upload_params();
    }

    void line_material::upload_params()
    {
        std::array<float, 4> payload{};
        payload[0] = static_cast<float>(m_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_color.a) / 255.0f;

        device().write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
