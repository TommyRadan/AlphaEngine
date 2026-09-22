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

#include <rendering_engine/materials/standard_material.hpp>

#include <array>
#include <string>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

namespace
{
    // std140 layout for the per-material params UBO, six vec4 rows:
    //   0  baseColor   (rgb tint, a alpha)
    //   16 emissive    (rgb colour, a intensity)
    //   32 params      (x metalness, y roughness, z opacity)
    //   48 mapFlags    (x albedo, y normal, z metalness, w roughness)
    //   64 mapFlags2   (x emissive)
    //   80 iblParams   (x ibl enabled, y ibl intensity)
    // 96 bytes total.
    constexpr size_t material_ubo_size = 96;

    // This material's stages, by shader-library path (see shaders/materials/).
    const rendering_engine::gpu::shader_variant vertex_shader{"materials/standard.vert.glsl"};
    const rendering_engine::gpu::shader_variant fragment_shader{"materials/standard.frag.glsl"};
} // namespace

namespace rendering_engine
{
    standard_material::standard_material(gpu::bind_group_layout frame_layout)
    {
        // Position+uv+normal+tangent stream; the layout helper bakes the
        // attribute offsets that match vertex_position_uv_normal_tangent.
        gpu::vertex_buffer_layout vertex_layout = vertex_position_uv_normal_tangent_layout();
        vertex_layout.stride = 0;
        m_vertex_format = vertex_format::position_uv_normal_tangent;

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching every 3D renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});

        // Per-material layout (slot 2): the params UBO plus the five PBR
        // samplers, all owned by this material.
        gpu::bind_group_layout_descriptor material_layout{};
        material_layout.entries.push_back({gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_normal_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_metalness_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_roughness_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_emissive_map, gpu::binding_kind::texture});
        // irradiance and prefiltered are samplerCube; flag them so a
        // backend that placeholders an unbound slot picks a cube, not a
        // 2D, texture when no environment is attached. brdfLut is 2D.
        gpu::bind_group_layout_entry irradiance_entry{gpu::shader_bindings::material_irradiance_map,
                                                      gpu::binding_kind::texture};
        irradiance_entry.dimension = gpu::texture_dimension::cube;
        material_layout.entries.push_back(irradiance_entry);
        gpu::bind_group_layout_entry prefiltered_entry{gpu::shader_bindings::material_prefiltered_map,
                                                       gpu::binding_kind::texture};
        prefiltered_entry.dimension = gpu::texture_dimension::cube;
        material_layout.entries.push_back(prefiltered_entry);
        material_layout.entries.push_back({gpu::shader_bindings::material_brdf_lut, gpu::binding_kind::texture});

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

    standard_material::~standard_material()
    {
        // Drop the bind group before the buffers / textures it
        // references, then null it so the base destructor's
        // destruct_pipeline does not double-free.
        auto& gpu = *runtime::current_engine().gpu;
        if (m_per_material_bind_group.valid())
        {
            gpu.destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
        release_map(m_emissive_map);
        release_map(m_roughness_map);
        release_map(m_metalness_map);
        release_map(m_normal_map);
        release_map(m_albedo_map);
        if (m_material_ubo.valid())
        {
            gpu.destroy(m_material_ubo);
            m_material_ubo = {};
        }
    }

    uint32_t standard_material::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    void standard_material::set_base_color(const util::color& color)
    {
        m_base_color = color;
        upload_params();
    }

    void standard_material::set_metalness(float metalness)
    {
        m_metalness = metalness;
        upload_params();
    }

    void standard_material::set_roughness(float roughness)
    {
        m_roughness = roughness;
        upload_params();
    }

    void standard_material::set_emissive(const util::color& color)
    {
        m_emissive = color;
        upload_params();
    }

    void standard_material::set_emissive_intensity(float intensity)
    {
        m_emissive_intensity = intensity;
        upload_params();
    }

    void standard_material::set_albedo_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_albedo_map);
        m_albedo_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_albedo_map()
    {
        if (!m_albedo_map.valid())
        {
            return;
        }
        release_map(m_albedo_map);
        rebuild_bind_group();
    }

    void standard_material::set_normal_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_normal_map);
        m_normal_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_normal_map()
    {
        if (!m_normal_map.valid())
        {
            return;
        }
        release_map(m_normal_map);
        rebuild_bind_group();
    }

    void standard_material::set_metalness_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_metalness_map);
        m_metalness_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_metalness_map()
    {
        if (!m_metalness_map.valid())
        {
            return;
        }
        release_map(m_metalness_map);
        rebuild_bind_group();
    }

    void standard_material::set_roughness_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_roughness_map);
        m_roughness_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_roughness_map()
    {
        if (!m_roughness_map.valid())
        {
            return;
        }
        release_map(m_roughness_map);
        rebuild_bind_group();
    }

    void standard_material::set_emissive_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_emissive_map);
        m_emissive_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_emissive_map()
    {
        if (!m_emissive_map.valid())
        {
            return;
        }
        release_map(m_emissive_map);
        rebuild_bind_group();
    }

    void standard_material::set_environment(const environment& env)
    {
        m_environment = &env;
        rebuild_bind_group();
    }

    void standard_material::clear_environment()
    {
        if (m_environment == nullptr)
        {
            return;
        }
        m_environment = nullptr;
        rebuild_bind_group();
    }

    gpu::texture standard_material::upload_map(const util::image& image, gpu::color_space space)
    {
        auto& gpu = *runtime::current_engine().gpu;

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
        gpu::texture map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(util::color);
        gpu.write_texture(map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(map);
        return map;
    }

    void standard_material::release_map(gpu::texture& map)
    {
        if (!map.valid())
        {
            return;
        }
        auto& gpu = *runtime::current_engine().gpu;
        gpu.destroy(map);
        map = {};
    }

    void standard_material::rebuild_bind_group()
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

        const std::array<std::pair<uint32_t, gpu::texture>, 5> maps = {{
            {gpu::shader_bindings::material_albedo_map, m_albedo_map},
            {gpu::shader_bindings::material_normal_map, m_normal_map},
            {gpu::shader_bindings::material_metalness_map, m_metalness_map},
            {gpu::shader_bindings::material_roughness_map, m_roughness_map},
            {gpu::shader_bindings::material_emissive_map, m_emissive_map},
        }};
        for (const auto& [binding, texture] : maps)
        {
            gpu::binding_value tex_slot{};
            tex_slot.binding = binding;
            tex_slot.kind = gpu::binding_kind::texture;
            tex_slot.texture_value = texture;
            bg_descriptor.entries.push_back(tex_slot);
        }

        // IBL set: bind the environment's tables, or invalid handles when
        // none is attached. The iblParams flag gates the sampling, so the
        // dormant handles are never read — matching how the scene pass
        // leaves the shadow map invalid until a caster exists.
        const std::array<std::pair<uint32_t, gpu::texture>, 3> ibl_maps = {{
            {gpu::shader_bindings::material_irradiance_map,
             m_environment != nullptr ? m_environment->irradiance() : gpu::texture{}},
            {gpu::shader_bindings::material_prefiltered_map,
             m_environment != nullptr ? m_environment->prefiltered() : gpu::texture{}},
            {gpu::shader_bindings::material_brdf_lut,
             m_environment != nullptr ? m_environment->brdf_lut() : gpu::texture{}},
        }};
        for (const auto& [binding, texture] : ibl_maps)
        {
            gpu::binding_value tex_slot{};
            tex_slot.binding = binding;
            tex_slot.kind = gpu::binding_kind::texture;
            tex_slot.texture_value = texture;
            bg_descriptor.entries.push_back(tex_slot);
        }

        m_per_material_bind_group = gpu.create_bind_group(bg_descriptor);

        upload_params();
    }

    void standard_material::upload_params()
    {
        std::array<float, 24> payload{};
        payload[0] = static_cast<float>(m_base_color.r) / 255.0f;
        payload[1] = static_cast<float>(m_base_color.g) / 255.0f;
        payload[2] = static_cast<float>(m_base_color.b) / 255.0f;
        payload[3] = static_cast<float>(m_base_color.a) / 255.0f;
        payload[4] = static_cast<float>(m_emissive.r) / 255.0f;
        payload[5] = static_cast<float>(m_emissive.g) / 255.0f;
        payload[6] = static_cast<float>(m_emissive.b) / 255.0f;
        payload[7] = m_emissive_intensity;
        payload[8] = m_metalness;
        payload[9] = m_roughness;
        payload[10] = m_params.opacity;
        payload[12] = m_albedo_map.valid() ? 1.0f : 0.0f;
        payload[13] = m_normal_map.valid() ? 1.0f : 0.0f;
        payload[14] = m_metalness_map.valid() ? 1.0f : 0.0f;
        payload[15] = m_roughness_map.valid() ? 1.0f : 0.0f;
        payload[16] = m_emissive_map.valid() ? 1.0f : 0.0f;
        // iblParams row at offset 80 (float index 20): enable flag + the
        // intensity multiplier the shader applies to the ambient term.
        payload[20] = m_environment != nullptr ? 1.0f : 0.0f;
        payload[21] = 1.0f;

        auto& gpu = *runtime::current_engine().gpu;
        gpu.write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
