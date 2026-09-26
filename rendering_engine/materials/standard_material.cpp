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
#include <utility>

#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace
{
    // std140 layout for the per-material params UBO, four vec4 rows:
    //   0  baseColor   (rgb tint, a alpha)
    //   16 emissive    (rgb colour, a intensity)
    //   32 params      (x metalness, y roughness, z opacity, w occlusion strength)
    //   48 iblParams   (x ibl enabled, y ibl intensity)
    // 64 bytes total. Which maps are sampled is a compile-time keyword,
    // not a flag in here.
    constexpr size_t material_ubo_size = 64;

    // Attribute location of the tangent in the position+uv+normal+tangent
    // stream (see vertex_position_uv_normal_tangent_layout).
    constexpr uint32_t tangent_location = 3;

    // Every keyword the map set can turn on; the tangent flag joins them.
    constexpr uint32_t keywords_default =
        rendering_engine::keyword_bit(rendering_engine::material_keyword::has_tangents);
} // namespace

namespace rendering_engine
{
    material_template_descriptor standard_material::template_descriptor(gpu::bind_group_layout frame_layout)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "standard";
        descriptor.vertex_shader = gpu::shader_variant{"materials/standard.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/standard.frag.glsl"};

        // Position+uv+normal+tangent stream; the layout helper bakes the
        // attribute offsets that match vertex_position_uv_normal_tangent.
        // A variant without HAS_TANGENTS drops the tangent attribute and
        // reads the position+uv+normal prefix instead.
        gpu::vertex_buffer_layout vertex_layout = vertex_position_uv_normal_tangent_layout();
        vertex_layout.stride = 0;
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = vertex_format::position_uv_normal_tangent;
        descriptor.vertex_format_without_tangents = vertex_format::position_uv_normal;
        descriptor.tangent_location = tangent_location;

        // Per-draw layout (slot 1): the model + normal matrix UBO at
        // binding 1, matching every 3D renderable's bind group.
        descriptor.draw_layout.entries.push_back(
            {gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});
        descriptor.frame_layout = frame_layout;

        // Per-material layout (slot 2): the params UBO plus the PBR
        // samplers, all owned by each instance. The layout is the same
        // for every variant; a variant simply leaves unused slots
        // unsampled.
        auto& material_layout = descriptor.material_layout;
        material_layout.entries.push_back({gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
        material_layout.entries.push_back({gpu::shader_bindings::material_albedo_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_normal_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_metalness_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_roughness_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_emissive_map, gpu::binding_kind::texture});
        material_layout.entries.push_back({gpu::shader_bindings::material_occlusion_map, gpu::binding_kind::texture});
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

        descriptor.topology = gpu::primitive_topology::triangles;
        return descriptor;
    }

    std::shared_ptr<material_template> standard_material::create_template(gpu::device& device,
                                                                          gpu::bind_group_layout frame_layout)
    {
        return std::make_shared<material_template>(device, template_descriptor(frame_layout));
    }

    standard_material::standard_material(std::shared_ptr<material_template> tmpl)
        // Opaque lit surface: depth tested and written, no blending. The
        // tangent keyword is on until set_tangents says otherwise.
        : material(std::move(tmpl), material_params{}, keywords_default)
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = material_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_material_ubo = device().create_buffer(ubo_descriptor);

        rebuild_bind_group();
    }

    standard_material::~standard_material()
    {
        // Drop the bind group before the buffers / textures it
        // references.
        release_per_material_bind_group();
        release_map(m_orm_map);
        release_map(m_occlusion_map);
        release_map(m_emissive_map);
        release_map(m_roughness_map);
        release_map(m_metalness_map);
        release_map(m_normal_map);
        release_map(m_albedo_map);
        if (m_material_ubo.valid())
        {
            device().destroy(m_material_ubo);
            m_material_ubo = {};
        }
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

    void standard_material::set_occlusion_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_occlusion_map);
        m_occlusion_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_occlusion_map()
    {
        if (!m_occlusion_map.valid())
        {
            return;
        }
        release_map(m_occlusion_map);
        rebuild_bind_group();
    }

    void standard_material::set_orm_map(const util::image& image, gpu::color_space space)
    {
        release_map(m_orm_map);
        m_orm_map = upload_map(image, space);
        rebuild_bind_group();
    }

    void standard_material::clear_orm_map()
    {
        if (!m_orm_map.valid())
        {
            return;
        }
        release_map(m_orm_map);
        rebuild_bind_group();
    }

    void standard_material::set_occlusion_strength(float strength)
    {
        m_occlusion_strength = strength;
        upload_params();
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

    void standard_material::set_ibl_intensity(float intensity)
    {
        m_ibl_intensity = intensity;
        upload_params();
    }

    void standard_material::set_tangents(bool enabled)
    {
        if (m_tangents == enabled)
        {
            return;
        }
        m_tangents = enabled;
        update_keywords();
    }

    bool standard_material::has_tangents() const
    {
        return (keywords() & keyword_bit(material_keyword::has_tangents)) != 0;
    }

    bool standard_material::uses_orm_map() const
    {
        return (keywords() & keyword_bit(material_keyword::use_orm_map)) != 0;
    }

    void standard_material::on_params_changed()
    {
        upload_params();
    }

    void standard_material::update_keywords()
    {
        uint32_t mask = 0;
        if (m_tangents)
        {
            mask |= keyword_bit(material_keyword::has_tangents);
        }
        if (m_albedo_map.valid())
        {
            mask |= keyword_bit(material_keyword::use_albedo_map);
        }
        // Normal mapping needs the tangent frame; without it the map
        // stays bound but the variant has no code to sample it.
        if (m_normal_map.valid() && m_tangents)
        {
            mask |= keyword_bit(material_keyword::use_normal_map);
        }
        if (m_orm_map.valid())
        {
            // The packed map supersedes the two single-channel ones, so
            // their keywords stay off and no extra variant is compiled.
            mask |= keyword_bit(material_keyword::use_orm_map);
        }
        else
        {
            if (m_metalness_map.valid())
            {
                mask |= keyword_bit(material_keyword::use_metallic_map);
            }
            if (m_roughness_map.valid())
            {
                mask |= keyword_bit(material_keyword::use_roughness_map);
            }
        }
        // A separate occlusion map overrides the packed map's R channel,
        // so it stays independent of the ORM keyword.
        if (m_occlusion_map.valid())
        {
            mask |= keyword_bit(material_keyword::use_occlusion_map);
        }
        if (m_emissive_map.valid())
        {
            mask |= keyword_bit(material_keyword::use_emissive_map);
        }
        set_keywords(mask);
    }

    void standard_material::rebuild_bind_group()
    {
        auto& gpu = device();
        release_per_material_bind_group();

        gpu::bind_group_descriptor bg_descriptor{};
        bg_descriptor.layout = get_template().per_material_layout();

        gpu::binding_value ubo_slot{};
        ubo_slot.binding = gpu::shader_bindings::material_params;
        ubo_slot.kind = gpu::binding_kind::uniform_buffer;
        ubo_slot.buffer_value = m_material_ubo;
        bg_descriptor.entries.push_back(ubo_slot);

        // The metalness slot carries the packed ORM map when one is
        // bound; the USE_ORM_MAP variant reads roughness, metalness and
        // (absent a separate occlusion map) occlusion from it.
        const std::array<std::pair<uint32_t, gpu::texture>, 6> maps = {{
            {gpu::shader_bindings::material_albedo_map, m_albedo_map},
            {gpu::shader_bindings::material_normal_map, m_normal_map},
            {gpu::shader_bindings::material_metalness_map, m_orm_map.valid() ? m_orm_map : m_metalness_map},
            {gpu::shader_bindings::material_roughness_map, m_roughness_map},
            {gpu::shader_bindings::material_emissive_map, m_emissive_map},
            {gpu::shader_bindings::material_occlusion_map, m_occlusion_map},
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

        update_keywords();
        upload_params();
    }

    void standard_material::upload_params()
    {
        std::array<float, 16> payload{};
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
        payload[10] = params().opacity;
        payload[11] = m_occlusion_strength;
        // iblParams row at offset 48 (float index 12): enable flag + the
        // intensity multiplier the shader applies to the ambient term.
        payload[12] = m_environment != nullptr ? 1.0f : 0.0f;
        payload[13] = m_ibl_intensity;

        device().write_buffer(m_material_ubo, payload.data(), material_ubo_size, 0);
    }
} // namespace rendering_engine
