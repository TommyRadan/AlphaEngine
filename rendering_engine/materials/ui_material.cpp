// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/ui_material.hpp>

#include <cstddef>
#include <utility>

#include <rendering_engine/gpu/device.hpp>

namespace
{
    // Overlay pass disables depth testing entirely so 2D elements
    // draw in submission order; depth writes also stay off so the
    // overlay never trashes the scene's depth buffer. Double-sided:
    // the y-down projection flips every quad's winding.
    rendering_engine::material_params ui_params()
    {
        rendering_engine::material_params params{};
        params.transparent = true;
        params.blending = rendering_engine::blend_mode::normal;
        params.double_sided = true;
        params.depth_test = false;
        params.depth_write = false;
        return params;
    }

    rendering_engine::gpu::vertex_attribute float2_attribute(uint32_t location, std::size_t offset)
    {
        return {location, 2, rendering_engine::gpu::scalar_type::float32, static_cast<uint32_t>(offset)};
    }
} // namespace

namespace rendering_engine
{
    gpu::bind_group_layout_descriptor ui_material::frame_layout_descriptor()
    {
        gpu::bind_group_layout_descriptor descriptor{};
        descriptor.entries.push_back({frame_binding, gpu::binding_kind::uniform_buffer});
        return descriptor;
    }

    std::shared_ptr<material_template> ui_material::create_template(gpu::device& device,
                                                                    gpu::bind_group_layout frame_layout)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "ui";
        descriptor.vertex_shader = gpu::shader_variant{"materials/ui.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/ui.frag.glsl"};

        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = sizeof(ui_vertex);
        vertex_layout.attributes.push_back(float2_attribute(0, offsetof(ui_vertex, pivot_anchor)));
        vertex_layout.attributes.push_back(float2_attribute(1, offsetof(ui_vertex, pivot_offset)));
        vertex_layout.attributes.push_back(float2_attribute(2, offsetof(ui_vertex, corner_anchor)));
        vertex_layout.attributes.push_back(float2_attribute(3, offsetof(ui_vertex, corner_offset)));
        vertex_layout.attributes.push_back(float2_attribute(4, offsetof(ui_vertex, rotation)));
        vertex_layout.attributes.push_back(float2_attribute(5, offsetof(ui_vertex, uv)));
        gpu::vertex_attribute color_attribute{
            6, 4, gpu::scalar_type::uint8, static_cast<uint32_t>(offsetof(ui_vertex, color))};
        color_attribute.normalized = true;
        vertex_layout.attributes.push_back(color_attribute);
        descriptor.vertex_layouts.push_back(vertex_layout);
        // The record has no named vertex_format; the stride check covers it.
        descriptor.required_vertex_format = vertex_format::custom;
        descriptor.vertex_format_without_tangents = vertex_format::custom;

        // Slot 0 is the ui pass's per-frame group (the UiFrame block at
        // binding 0); the per-draw group at slot 1 carries the texture at
        // binding 1, a number of its own so it stays unique across the
        // pipeline's sets as the OpenGL backend requires.
        descriptor.frame_layout = frame_layout;
        descriptor.draw_layout.entries.push_back({texture_binding, gpu::binding_kind::texture});

        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    ui_material::ui_material(std::shared_ptr<material_template> tmpl) : material(std::move(tmpl), ui_params())
    {
        // Linear, so the white is exactly 1.0 in every channel whatever
        // the colour space; clamped nearest, one texel.
        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::texture_format::rgba8_unorm;
        descriptor.width = 1;
        descriptor.height = 1;
        descriptor.min_filter = gpu::filter_mode::nearest;
        descriptor.mag_filter = gpu::filter_mode::nearest;
        descriptor.address_u = gpu::address_mode::clamp_edge;
        descriptor.address_v = gpu::address_mode::clamp_edge;
        descriptor.address_w = gpu::address_mode::clamp_edge;
        m_white_texture = device().create_texture(descriptor);
        const color white{255, 255, 255, 255};
        device().write_texture(m_white_texture, &white, sizeof(white));
    }

    ui_material::~ui_material()
    {
        if (m_white_texture.valid())
        {
            device().destroy(m_white_texture);
        }
    }

    gpu::texture ui_material::white_texture() const
    {
        return m_white_texture;
    }
} // namespace rendering_engine
