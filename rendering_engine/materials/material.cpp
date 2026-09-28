// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/material.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <assets/color.hpp>
#include <core/log.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/resources/texture_asset.hpp>
#include <rendering_engine/resources/texture_formats.hpp>

namespace rendering_engine
{
    material::material(std::shared_ptr<material_template> tmpl) : material(tmpl, tmpl->descriptor().defaults) {}

    material::material(std::shared_ptr<material_template> tmpl, const material_params& params, uint32_t keywords)
        : m_template(std::move(tmpl)), m_params(params), m_keywords(keywords)
    {
        rebind_variant();
        m_template->register_instance(this);
        create_declared_resources();
    }

    material::~material()
    {
        // Instances are released while the device is live (before
        // rendering_engine::renderer::quit tears it down); the template
        // they share outlives them through the shared handle.
        m_template->unregister_instance(this);
        release_per_material_bind_group();
        if (m_parameter_buffer.valid())
        {
            device().destroy(m_parameter_buffer);
            m_parameter_buffer = {};
        }
    }

    material_template& material::get_template() const
    {
        return *m_template;
    }

    gpu::pipeline material::pipeline() const
    {
        return m_pipeline;
    }

    gpu::pipeline material::pipeline(bool mirrored)
    {
        if (!mirrored)
        {
            return m_pipeline;
        }
        if (!m_mirrored_pipeline.valid())
        {
            m_mirrored_pipeline = m_template->pipeline(facing_key(true));
        }
        return m_mirrored_pipeline;
    }

    bool material::draws_in_depth_prepass() const
    {
        // The queue test is the scene pass's own (transparent picks the
        // transparent queue), and a transparent key never writes depth
        // anyway. A variant that skips the test or the write must not be
        // pre-passed either: depth it never writes in the scene pass
        // would otherwise start occluding other surfaces.
        return m_template->descriptor().depth_prepass && !m_params.transparent && m_key.depth_test && m_key.depth_write;
    }

    gpu::pipeline material::depth_prepass_pipeline(bool mirrored)
    {
        gpu::pipeline& twin = m_depth_prepass_pipelines[mirrored ? 1 : 0];
        if (!twin.valid())
        {
            twin = m_template->pipeline(depth_prepass_variant(facing_key(mirrored)));
        }
        return twin;
    }

    gpu::pipeline material::depth_prepassed_pipeline(bool mirrored)
    {
        gpu::pipeline& twin = m_depth_prepassed_pipelines[mirrored ? 1 : 0];
        if (!twin.valid())
        {
            twin = m_template->pipeline(depth_prepassed_variant(facing_key(mirrored)));
        }
        return twin;
    }

    const pipeline_variant_key& material::variant_key() const
    {
        return m_key;
    }

    const material_params& material::params() const
    {
        return m_params;
    }

    uint32_t material::keywords() const
    {
        return m_keywords;
    }

    void material::set_params(const material_params& params)
    {
        m_params = params;
        rebind_variant();
        on_params_changed();
    }

    void material::set_transparent(bool transparent)
    {
        material_params params = m_params;
        params.transparent = transparent;
        set_params(params);
    }

    void material::set_opacity(float opacity)
    {
        material_params params = m_params;
        params.opacity = opacity;
        set_params(params);
    }

    void material::set_double_sided(bool double_sided)
    {
        material_params params = m_params;
        params.double_sided = double_sided;
        set_params(params);
    }

    void material::set_blending(blend_mode blending)
    {
        material_params params = m_params;
        params.blending = blending;
        set_params(params);
    }

    void material::set_wireframe(bool wireframe)
    {
        material_params params = m_params;
        params.wireframe = wireframe;
        set_params(params);
    }

    void material::set_depth_test(bool depth_test)
    {
        material_params params = m_params;
        params.depth_test = depth_test;
        set_params(params);
    }

    void material::set_depth_write(bool depth_write)
    {
        material_params params = m_params;
        params.depth_write = depth_write;
        set_params(params);
    }

    void material::set_fog(bool fog)
    {
        material_params params = m_params;
        params.fog = fog;
        set_params(params);
    }

    gpu::bind_group_layout material::per_draw_layout() const
    {
        return m_template->per_draw_layout(m_key.keywords);
    }

    bool material::is_skinned() const
    {
        return m_template->skins(m_key.keywords);
    }

    uint32_t material::per_draw_slot() const
    {
        return m_template->per_draw_slot();
    }

    uint32_t material::per_material_slot() const
    {
        return m_template->per_material_slot();
    }

    gpu::bind_group material::per_material_bind_group() const
    {
        return m_per_material_bind_group;
    }

    assets::vertex_format material::required_vertex_format() const
    {
        return m_template->required_vertex_format(m_key.keywords);
    }

    uint32_t material::min_vertex_stride() const
    {
        return m_template->min_vertex_stride(m_key.keywords);
    }

    gpu::device& material::device() const
    {
        return m_template->device();
    }

    void material::set_keyword(material_keyword keyword, bool enabled)
    {
        const uint32_t bit = keyword_bit(keyword);
        set_keywords(enabled ? (m_keywords | bit) : (m_keywords & ~bit));
    }

    void material::set_keywords(uint32_t keywords)
    {
        if (keywords == m_keywords)
        {
            return;
        }
        m_keywords = keywords;
        rebind_variant();
    }

    void material::release_per_material_bind_group()
    {
        if (m_per_material_bind_group.valid())
        {
            device().destroy(m_per_material_bind_group);
            m_per_material_bind_group = {};
        }
    }

    gpu::texture
    material::upload_map(const assets::image& image, assets::color_space space, gpu::address_mode address) const
    {
        auto& gpu = device();

        gpu::texture_descriptor descriptor{};
        descriptor.dimension = gpu::texture_dimension::d2;
        descriptor.format = rgba8_format(space);
        descriptor.width = image.get_width();
        descriptor.height = image.get_height();
        descriptor.mipmaps = true;
        descriptor.min_filter = gpu::filter_mode::linear;
        descriptor.mag_filter = gpu::filter_mode::linear;
        descriptor.address_u = address;
        descriptor.address_v = address;
        descriptor.address_w = address;
        gpu::texture map = gpu.create_texture(descriptor);

        const size_t pixel_bytes =
            static_cast<size_t>(image.get_width()) * static_cast<size_t>(image.get_height()) * sizeof(assets::color);
        gpu.write_texture(map, image.get_pixels(), pixel_bytes);
        gpu.generate_mipmaps(map);
        return map;
    }

    void material::release_map(gpu::texture& map) const
    {
        if (!map.valid())
        {
            return;
        }
        device().destroy(map);
        map = {};
    }

    bool material::set_float(std::string_view name, float value)
    {
        return write_parameter(name, material_parameter_type::float1, &value, sizeof(value));
    }

    bool material::set_float2(std::string_view name, const core::math::vec2& value)
    {
        const float data[2] = {value.x, value.y};
        return write_parameter(name, material_parameter_type::float2, data, sizeof(data));
    }

    bool material::set_float3(std::string_view name, const core::math::vec3& value)
    {
        const float data[3] = {value.x, value.y, value.z};
        return write_parameter(name, material_parameter_type::float3, data, sizeof(data));
    }

    bool material::set_float4(std::string_view name, const core::math::vec4& value)
    {
        const float data[4] = {value.x, value.y, value.z, value.w};
        return write_parameter(name, material_parameter_type::float4, data, sizeof(data));
    }

    bool material::set_int(std::string_view name, int32_t value)
    {
        return write_parameter(name, material_parameter_type::int1, &value, sizeof(value));
    }

    bool material::set_uint(std::string_view name, uint32_t value)
    {
        return write_parameter(name, material_parameter_type::uint1, &value, sizeof(value));
    }

    bool material::set_texture(std::string_view name, std::shared_ptr<texture_asset> texture)
    {
        const int slot = m_template->find_texture(name);
        if (slot < 0 || static_cast<std::size_t>(slot) >= m_textures.size())
        {
            warn_once(name, "declares no texture slot of that name");
            return false;
        }
        m_textures[static_cast<std::size_t>(slot)].asset = std::move(texture);
        if (const uint32_t bit = m_template->texture_keyword_bit(static_cast<std::size_t>(slot)); bit != 0)
        {
            const bool bound = m_textures[static_cast<std::size_t>(slot)].asset != nullptr;
            set_keywords(bound ? (m_keywords | bit) : (m_keywords & ~bit));
        }
        rebuild_declared_bind_group();
        return true;
    }

    bool material::set_keyword_enabled(std::string_view name, bool enabled)
    {
        const auto& declared = m_template->descriptor().keywords;
        if (std::find(declared.begin(), declared.end(), name) == declared.end())
        {
            warn_once(name, "declares no keyword of that name");
            return false;
        }
        const uint32_t bit = m_template->keyword_bit_for(name);
        set_keywords(enabled ? (m_keywords | bit) : (m_keywords & ~bit));
        return true;
    }

    bool material::refresh_texture_assets()
    {
        const bool stale =
            std::any_of(m_textures.begin(),
                        m_textures.end(),
                        [](const texture_binding& binding)
                        { return binding.asset != nullptr && binding.asset->generation != binding.generation; });
        if (!stale)
        {
            return false;
        }
        rebuild_declared_bind_group();
        return true;
    }

    void material::create_declared_resources()
    {
        if (!m_template->declares_resources())
        {
            return;
        }
        const material_template_descriptor& descriptor = m_template->descriptor();
        m_textures.resize(descriptor.textures.size());

        m_parameter_data.assign(descriptor.parameter_block_size, std::byte{0});
        for (const material_parameter& parameter : descriptor.parameters)
        {
            std::byte* target = m_parameter_data.data() + parameter.offset;
            const float components[4] = {parameter.default_value.x,
                                         parameter.default_value.y,
                                         parameter.default_value.z,
                                         parameter.default_value.w};
            if (parameter.type == material_parameter_type::int1)
            {
                const auto value = static_cast<int32_t>(std::lround(parameter.default_value.x));
                std::memcpy(target, &value, sizeof(value));
            }
            else if (parameter.type == material_parameter_type::uint1)
            {
                const auto value = static_cast<uint32_t>(std::max(0l, std::lround(parameter.default_value.x)));
                std::memcpy(target, &value, sizeof(value));
            }
            else
            {
                std::memcpy(target, components, material_parameter_size(parameter.type));
            }
        }
        if (!m_parameter_data.empty())
        {
            gpu::buffer_descriptor buffer_descriptor{};
            buffer_descriptor.size = m_parameter_data.size();
            buffer_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            buffer_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            buffer_descriptor.initial_data = m_parameter_data.data();
            m_parameter_buffer = device().create_buffer(buffer_descriptor);
        }
        rebuild_declared_bind_group();
    }

    void material::rebuild_declared_bind_group()
    {
        release_per_material_bind_group();

        gpu::bind_group_descriptor descriptor{};
        descriptor.layout = m_template->per_material_layout();
        if (m_parameter_buffer.valid())
        {
            gpu::binding_value block{};
            block.binding = gpu::shader_bindings::material_params;
            block.kind = gpu::binding_kind::uniform_buffer;
            block.buffer_value = m_parameter_buffer;
            descriptor.entries.push_back(block);
        }
        const auto& slots = m_template->descriptor().textures;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            texture_binding& binding = m_textures[i];
            binding.generation = binding.asset != nullptr ? binding.asset->generation : 0;
            // An empty slot stays invalid: the device binds its placeholder
            // of the slot's dimension there.
            gpu::binding_value map{};
            map.binding = slots[i].binding;
            map.kind = gpu::binding_kind::texture;
            map.texture_value = binding.asset != nullptr ? binding.asset->texture : gpu::texture{};
            descriptor.entries.push_back(map);
        }
        m_per_material_bind_group = device().create_bind_group(descriptor);
    }

    bool
    material::write_parameter(std::string_view name, material_parameter_type type, const void* value, uint32_t size)
    {
        const material_parameter* parameter = m_template->find_parameter(name);
        if (parameter == nullptr)
        {
            warn_once(name, "declares no parameter of that name");
            return false;
        }
        if (parameter->type != type)
        {
            warn_once(name, "declares that parameter with another type");
            return false;
        }
        std::memcpy(m_parameter_data.data() + parameter->offset, value, size);
        device().write_buffer(m_parameter_buffer, m_parameter_data.data(), m_parameter_data.size(), 0);
        return true;
    }

    void material::warn_once(std::string_view name, const char* message)
    {
        if (std::find(m_warned.begin(), m_warned.end(), name) != m_warned.end())
        {
            return;
        }
        m_warned.emplace_back(name);
        LOG_WRN("material (template %s): '%.*s' ignored; the template %s",
                m_template->name().c_str(),
                static_cast<int>(name.size()),
                name.data(),
                message);
    }

    void material::rebind_variant()
    {
        const pipeline_variant_key key = make_pipeline_variant_key(m_params, m_keywords);
        if (key == m_key && m_pipeline.valid())
        {
            return;
        }
        m_key = key;
        m_pipeline = m_template->pipeline(key);
        // The clockwise and depth pre-pass twins belong to the old key;
        // the next draw that wants one resolves it again through the
        // cache.
        m_mirrored_pipeline = {};
        m_depth_prepass_pipelines = {};
        m_depth_prepassed_pipelines = {};
    }

    pipeline_variant_key material::facing_key(bool mirrored) const
    {
        pipeline_variant_key key = m_key;
        if (mirrored)
        {
            key.front = gpu::front_face::clockwise;
        }
        return key;
    }
} // namespace rendering_engine
