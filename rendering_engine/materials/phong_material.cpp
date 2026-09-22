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

#include <rendering_engine/materials/phong_material.hpp>

#include <array>
#include <string>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

namespace
{
    // std140 layout for the per-material params UBO: vec4 diffuseColor
    // at offset 0, vec4 specular (rgb colour, a shininess) at offset 16,
    // vec4 misc (x useTexture) at offset 32. The struct is 48 bytes.
    constexpr size_t material_ubo_size = 48;

    // This material's stages, by shader-library path (see shaders/materials/).
    // The diffuse map takes the shared albedo-map binding.
    const rendering_engine::gpu::shader_variant vertex_shader{"materials/phong.vert.glsl"};
    const rendering_engine::gpu::shader_variant fragment_shader{"materials/phong.frag.glsl"};
} // namespace

namespace rendering_engine
{
    phong_material::phong_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are supplied at
        // set_vertex_buffer time. Position sits at offset 0, UV at
        // offset 12 and normal at offset 20 across the
        // position+uv+normal vertex stream this pipeline draws, so the
        // baked attribute offsets stay valid.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});
        vertex_layout.attributes.push_back({2, 3, gpu::scalar_type::float32, sizeof(float) * 5});
        m_vertex_format = vertex_format::position_uv_normal;

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching every 3D renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the params UBO plus the diffuse
        // sampler, both owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});

        // Opaque lit surface: depth tested and written, no blending.
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

        rebuild_bind_group();
    }

    phong_material::~phong_material()
    {
        // Drop the bind group before the buffers / texture it
        // references, then null it so the base destructor's
        // destruct_pipeline does not double-free.
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        if (m_diffuse_map.valid())
        {
            gpu.destroy(m_diffuse_map);
            m_diffuse_map = {};
        }
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t phong_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void phong_material::set_diffuse(const util::color& color)
    {
        m_diffuse = color;
        upload_params();
    }

    void phong_material::set_specular(const util::color& color)
    {
        m_specular = color;
        upload_params();
    }

    void phong_material::set_shininess(float shininess)
    {
        m_shininess = shininess;
        upload_params();
    }

    void phong_material::set_diffuse_map(const util::image& image, gpu::color_space space)
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_diffuse_map.valid())
        {
            gpu.destroy(m_diffuse_map);
            m_diffuse_map = {};
        }

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = gpu::rgba8_format(space);
        descriptor.width = image.get_width();
        descriptor.height = image.get_height();
        descriptor.mipmaps = true;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = gpu::address_mode::repeat;
        descriptor.address_v = gpu::address_mode::repeat;
        descriptor.address_w = gpu::address_mode::repeat;
        m_diffuse_map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(m_diffuse_map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(m_diffuse_map);

        rebuild_bind_group();
    }

    void phong_material::clear_diffuse_map()
    {
        if (!m_diffuse_map.valid())
        {
            return;
        }
        auto& gpu = *runtime::current_engine().gpu;
        gpu.destroy(m_diffuse_map);
        m_diffuse_map = {};
        rebuild_bind_group();
    }

    void phong_material::rebuild_bind_group()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = m_per_material_layout;

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = gpu::shader_bindings::material_params;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        gpu::binding_value tex_slot{};
        tex_slot.binding = gpu::shader_bindings::material_albedo_map;
        tex_slot.kind = gpu::binding_kind::texture;
        tex_slot.texture_value = m_diffuse_map;
        bg_descriptor.entries.push_back(tex_slot);

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void phong_material::upload_params()
    {
        std::array<float, 12> payload{};
        payload[0] = static_cast<float>(m_diffuse.r) / 255.0f;
        payload[1] = static_cast<float>(m_diffuse.g) / 255.0f;
        payload[2] = static_cast<float>(m_diffuse.b) / 255.0f;
        payload[3] = static_cast<float>(m_diffuse.a) / 255.0f;
        payload[4] = static_cast<float>(m_specular.r) / 255.0f;
        payload[5] = static_cast<float>(m_specular.g) / 255.0f;
        payload[6] = static_cast<float>(m_specular.b) / 255.0f;
        payload[7] = m_shininess;
        payload[8] = m_diffuse_map.valid() ? 1.0f : 0.0f;

        auto& gpu = *runtime::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
