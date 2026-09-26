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

#include <rendering_engine/materials/basic_material.hpp>

#include <array>
#include <utility>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace
{
    // std140 layout for the per-material params UBO: vec4 color at
    // offset 0, float useTexture at offset 16. The struct rounds up to
    // 32 bytes.
    constexpr size_t material_ubo_size = 32;
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template> basic_material::create_template(gpu::device& device,
                                                                       gpu::bind_group_layout frame_layout)
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
        descriptor.required_vertex_format = vertex_format::position_uv;
        descriptor.vertex_format_without_tangents = vertex_format::position_uv;

        // Per-draw layout (slot 1): the model + normal matrix UBO at
        // binding 1, matching every 3D renderable's bind group.
        descriptor.draw_layout.entries.push_back(
            {gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});
        descriptor.frame_layout = frame_layout;

        // Per-material layout (slot 2): the {color, useTexture} UBO plus
        // the albedo sampler, both owned by each instance.
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        descriptor.material_layout.entries.push_back(
            {gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});

        return std::make_shared<material_template>(device, std::move(descriptor));
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

    void basic_material::set_color(const util::color& color)
    {
        m_color = color;
        upload_params();
    }

    void basic_material::set_albedo(const util::image& image, gpu::color_space space)
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
