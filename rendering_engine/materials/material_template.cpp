// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/material_template.hpp>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/gpu/shader_library.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

namespace
{
    namespace gpu = rendering_engine::gpu;

    uint32_t scalar_size(gpu::scalar_type type)
    {
        switch (type)
        {
        case gpu::scalar_type::float32:
        case gpu::scalar_type::int32:
        case gpu::scalar_type::uint32:
            return 4;
        case gpu::scalar_type::int16:
        case gpu::scalar_type::uint16:
            return 2;
        case gpu::scalar_type::int8:
        case gpu::scalar_type::uint8:
            return 1;
        }
        return 4;
    }

    // Byte extent of the furthest-reaching attribute in @p layout: the
    // narrowest vertex record the layout can be bound over without any
    // attribute fetch running past the end of the vertex.
    uint32_t layout_extent(const gpu::vertex_buffer_layout& layout)
    {
        uint32_t extent = 0;
        for (const auto& attribute : layout.attributes)
        {
            const uint32_t end = attribute.offset + attribute.components * scalar_size(attribute.type);
            extent = end > extent ? end : extent;
        }
        return extent;
    }

    // The slot-0 layout that reads every channel of the named @p format, in
    // record order at locations 0, 1, 2, ...; no attributes for a record
    // without a fixed channel list (custom, or the skinned one, whose
    // joints need the skinning fields of the descriptor).
    gpu::vertex_buffer_layout derived_vertex_layout(assets::vertex_format format)
    {
        struct channel
        {
            uint32_t components;
        };
        std::vector<channel> channels;
        switch (format)
        {
        case assets::vertex_format::position:
            channels = {{3}};
            break;
        case assets::vertex_format::position_color:
        case assets::vertex_format::position_normal:
            channels = {{3}, {3}};
            break;
        case assets::vertex_format::position_uv:
            channels = {{3}, {2}};
            break;
        case assets::vertex_format::position_color_normal:
            channels = {{3}, {3}, {3}};
            break;
        case assets::vertex_format::position_uv_normal:
            channels = {{3}, {2}, {3}};
            break;
        case assets::vertex_format::position_uv_normal_tangent:
            channels = {{3}, {2}, {3}, {4}};
            break;
        case assets::vertex_format::position_uv_normal_tangent_skin:
        case assets::vertex_format::custom:
            break;
        }
        gpu::vertex_buffer_layout layout{};
        // The renderable supplies the stride per draw.
        layout.stride = 0;
        uint32_t offset = 0;
        for (std::size_t i = 0; i < channels.size(); ++i)
        {
            layout.attributes.push_back(
                {static_cast<uint32_t>(i), channels[i].components, gpu::scalar_type::float32, offset});
            offset += channels[i].components * static_cast<uint32_t>(sizeof(float));
        }
        return layout;
    }

    bool derivable_vertex_format(assets::vertex_format format)
    {
        return !derived_vertex_layout(format).attributes.empty();
    }

    // The engine keyword whose define is @p name, as its key bit; 0 for none.
    uint32_t engine_keyword_bit(std::string_view name)
    {
        for (const rendering_engine::material_keyword keyword : rendering_engine::all_material_keywords)
        {
            if (name == rendering_engine::keyword_define(keyword))
            {
                return rendering_engine::keyword_bit(keyword);
            }
        }
        return 0;
    }

    // @p descriptor with what it leaves to derivation filled in: the
    // slot-0 layout from the vertex format, the tangent-less format, the
    // parameter block's size and the per-material layout from the
    // declared parameters and textures.
    rendering_engine::material_template_descriptor derive(rendering_engine::material_template_descriptor descriptor)
    {
        using rendering_engine::material_template_descriptor;
        if (descriptor.vertex_layouts.empty() && derivable_vertex_format(descriptor.required_vertex_format))
        {
            descriptor.vertex_layouts.push_back(derived_vertex_layout(descriptor.required_vertex_format));
        }
        if (descriptor.vertex_format_without_tangents == assets::vertex_format::custom &&
            descriptor.tangent_location == material_template_descriptor::no_tangent_location)
        {
            descriptor.vertex_format_without_tangents = descriptor.required_vertex_format;
        }
        if (descriptor.parameter_block_size == 0)
        {
            uint32_t end = 0;
            for (const rendering_engine::material_parameter& parameter : descriptor.parameters)
            {
                end = std::max(end, parameter.offset + rendering_engine::material_parameter_size(parameter.type));
            }
            descriptor.parameter_block_size = (end + 15u) & ~15u;
        }
        if (descriptor.material_layout.entries.empty())
        {
            if (descriptor.parameter_block_size != 0)
            {
                descriptor.material_layout.entries.push_back(
                    {gpu::shader_bindings::material_params, gpu::binding_kind::uniform_buffer});
            }
            for (const rendering_engine::material_texture_slot& slot : descriptor.textures)
            {
                gpu::bind_group_layout_entry entry{slot.binding, gpu::binding_kind::texture};
                entry.dimension = slot.dimension;
                descriptor.material_layout.entries.push_back(entry);
            }
        }
        return descriptor;
    }
} // namespace

namespace rendering_engine
{
    material_template::material_template(gpu::device& device,
                                         material_template_descriptor descriptor,
                                         gpu::bind_group_layout frame_layout)
        : m_device(&device), m_descriptor(derive(std::move(descriptor))), m_frame_layout(frame_layout)
    {
        // The per-draw layout is always created, even when empty, so the
        // slot numbering (frame, draw, material) is the same for every
        // template and the passes need no special cases.
        m_per_draw_layout = m_device->create_bind_group_layout(m_descriptor.draw_layout);
        if (!m_descriptor.material_layout.entries.empty())
        {
            m_per_material_layout = m_device->create_bind_group_layout(m_descriptor.material_layout);
        }
    }

    material_template::~material_template()
    {
        // Templates are released before the device tears its pools
        // down (the renderer's material library drops its materials, and
        // with them the templates they share, in material_library::quit),
        // so the device is live here.
        for (auto& [key, pipeline] : m_pipelines)
        {
            if (pipeline.valid())
            {
                m_device->destroy(pipeline);
            }
        }
        m_pipelines.clear();
        for (auto& [keywords, shaders] : m_shaders)
        {
            if (shaders.vertex.valid())
            {
                m_device->destroy(shaders.vertex);
            }
            if (shaders.fragment.valid())
            {
                m_device->destroy(shaders.fragment);
            }
        }
        m_shaders.clear();
        if (m_skinned_per_draw_layout.valid())
        {
            m_device->destroy(m_skinned_per_draw_layout);
            m_skinned_per_draw_layout = {};
        }
        if (m_per_material_layout.valid())
        {
            m_device->destroy(m_per_material_layout);
            m_per_material_layout = {};
        }
        if (m_per_draw_layout.valid())
        {
            m_device->destroy(m_per_draw_layout);
            m_per_draw_layout = {};
        }
    }

    gpu::device& material_template::device() const
    {
        return *m_device;
    }

    const std::string& material_template::name() const
    {
        return m_descriptor.name;
    }

    const material_template_descriptor& material_template::descriptor() const
    {
        return m_descriptor;
    }

    std::string material_template::validate(const material_template_descriptor& descriptor)
    {
        if (descriptor.name.empty())
        {
            return "the template has no name";
        }
        for (const gpu::shader_variant* stage : {&descriptor.vertex_shader, &descriptor.fragment_shader})
        {
            if (stage->path.empty() || !gpu::shader_library::contains(stage->path))
            {
                return "no shader named '" + stage->path + "' in the content directory's " +
                       std::string{gpu::shader_library::asset_directory} + "/ or the engine's";
            }
        }
        if (descriptor.vertex_layouts.empty() && !derivable_vertex_format(descriptor.required_vertex_format))
        {
            return std::string{"no vertex layout, and none can be derived from the vertex format "} +
                   assets::vertex_format_name(descriptor.required_vertex_format);
        }
        const bool declared = !descriptor.parameters.empty() || !descriptor.textures.empty();
        if (declared && !descriptor.material_layout.entries.empty())
        {
            return "both an explicit per-material layout and declared parameters or textures";
        }

        std::vector<const material_parameter*> by_offset;
        for (const material_parameter& parameter : descriptor.parameters)
        {
            if (parameter.name.empty())
            {
                return "a parameter has no name";
            }
            if (parameter.offset % material_parameter_alignment(parameter.type) != 0)
            {
                return "parameter '" + parameter.name + "' is off its std140 alignment";
            }
            if (descriptor.parameter_block_size != 0 &&
                parameter.offset + material_parameter_size(parameter.type) > descriptor.parameter_block_size)
            {
                return "parameter '" + parameter.name + "' ends past the parameter block";
            }
            for (const material_parameter* other : by_offset)
            {
                if (other->name == parameter.name)
                {
                    return "two parameters are named '" + parameter.name + "'";
                }
            }
            by_offset.push_back(&parameter);
        }
        std::sort(by_offset.begin(),
                  by_offset.end(),
                  [](const material_parameter* a, const material_parameter* b) { return a->offset < b->offset; });
        for (std::size_t i = 1; i < by_offset.size(); ++i)
        {
            if (by_offset[i - 1]->offset + material_parameter_size(by_offset[i - 1]->type) > by_offset[i]->offset)
            {
                return "parameters '" + by_offset[i - 1]->name + "' and '" + by_offset[i]->name + "' overlap";
            }
        }

        if (descriptor.keywords.size() > material_template_descriptor::max_template_keywords)
        {
            return "more than " + std::to_string(material_template_descriptor::max_template_keywords) + " keywords";
        }
        for (std::size_t i = 0; i < descriptor.keywords.size(); ++i)
        {
            const std::string& keyword = descriptor.keywords[i];
            if (keyword.empty() || engine_keyword_bit(keyword) != 0 ||
                std::find(descriptor.keywords.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                          descriptor.keywords.end(),
                          keyword) != descriptor.keywords.end())
            {
                return "keyword '" + keyword + "' is empty, an engine keyword or declared twice";
            }
        }

        for (std::size_t i = 0; i < descriptor.textures.size(); ++i)
        {
            const material_texture_slot& slot = descriptor.textures[i];
            if (slot.name.empty())
            {
                return "a texture slot has no name";
            }
            if (slot.binding == gpu::shader_bindings::material_params)
            {
                return "texture slot '" + slot.name + "' takes the parameter block's binding";
            }
            for (std::size_t j = 0; j < i; ++j)
            {
                if (descriptor.textures[j].name == slot.name || descriptor.textures[j].binding == slot.binding)
                {
                    return "texture slot '" + slot.name + "' repeats another slot's name or binding";
                }
            }
            if (!slot.keyword.empty() && engine_keyword_bit(slot.keyword) == 0 &&
                std::find(descriptor.keywords.begin(), descriptor.keywords.end(), slot.keyword) ==
                    descriptor.keywords.end())
            {
                return "texture slot '" + slot.name + "' names the unknown keyword '" + slot.keyword + "'";
            }
        }
        return {};
    }

    gpu::bind_group_layout material_template::frame_layout() const
    {
        return m_frame_layout;
    }

    const material_parameter* material_template::find_parameter(std::string_view name) const
    {
        for (const material_parameter& parameter : m_descriptor.parameters)
        {
            if (parameter.name == name)
            {
                return &parameter;
            }
        }
        return nullptr;
    }

    int material_template::find_texture(std::string_view name) const
    {
        for (std::size_t i = 0; i < m_descriptor.textures.size(); ++i)
        {
            if (m_descriptor.textures[i].name == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool material_template::declares_resources() const
    {
        return !m_descriptor.parameters.empty() || !m_descriptor.textures.empty();
    }

    uint32_t material_template::keyword_bit_for(std::string_view name) const
    {
        if (const uint32_t bit = engine_keyword_bit(name); bit != 0)
        {
            return bit;
        }
        for (std::size_t i = 0; i < m_descriptor.keywords.size(); ++i)
        {
            if (m_descriptor.keywords[i] == name)
            {
                return 1u << (material_template_descriptor::first_template_keyword + static_cast<uint32_t>(i));
            }
        }
        return 0;
    }

    uint32_t material_template::texture_keyword_bit(std::size_t slot) const
    {
        if (slot >= m_descriptor.textures.size() || m_descriptor.textures[slot].keyword.empty())
        {
            return 0;
        }
        return keyword_bit_for(m_descriptor.textures[slot].keyword);
    }

    gpu::pipeline material_template::pipeline(const pipeline_variant_key& key)
    {
        const uint64_t packed = key.pack();
        if (const auto found = m_pipelines.find(packed); found != m_pipelines.end())
        {
            return found->second;
        }

        const shader_set& shaders = shaders_for(key.keywords);

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = shaders.vertex;
        // A depth-only variant (the depth pre-pass's) draws into a target
        // with no colour attachment, so like the shadow passes it has no
        // fragment stage; its vertex stage is the very module the colour
        // variants of these keywords run.
        pipeline_descriptor.fragment_shader = key.depth_only ? gpu::shader_module{} : shaders.fragment;
        pipeline_descriptor.vertex_buffers = vertex_layouts_for(key.keywords);
        pipeline_descriptor.topology = m_descriptor.topology;
        pipeline_descriptor.depth = to_depth_state(key);
        pipeline_descriptor.blend = to_blend_state(key.blending);
        pipeline_descriptor.rasterizer = to_rasterizer_state(key);
        if (m_frame_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_frame_layout);
        }
        pipeline_descriptor.bind_group_layouts.push_back(per_draw_layout(key.keywords));
        if (m_per_material_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_per_material_layout);
        }
        // Every variant of every template declares the PerDraw block's
        // push-constant range, whether its shaders read the block or not
        // (the instanced and ui ones do not): a pass binds its per-frame
        // group once and switches between these pipelines under it, which
        // Vulkan allows only across layouts with identical push-constant
        // ranges.
        pipeline_descriptor.push_constant_ranges.push_back(per_draw_push_constant_range());

        const gpu::pipeline built = m_device->create_pipeline(pipeline_descriptor);
        m_pipelines.emplace(packed, built);
        LOG_DBG("material_template %s: built pipeline variant %llx (%zu variants)",
                m_descriptor.name.c_str(),
                static_cast<unsigned long long>(packed),
                m_pipelines.size());
        return built;
    }

    bool material_template::has_variant(const pipeline_variant_key& key) const
    {
        return m_pipelines.find(key.pack()) != m_pipelines.end();
    }

    std::size_t material_template::variant_count() const
    {
        return m_pipelines.size();
    }

    std::size_t material_template::shader_set_count() const
    {
        return m_shaders.size();
    }

    gpu::bind_group_layout material_template::per_draw_layout() const
    {
        return m_per_draw_layout;
    }

    gpu::bind_group_layout material_template::per_draw_layout(uint32_t keywords) const
    {
        if (!skins(keywords))
        {
            return m_per_draw_layout;
        }
        if (!m_skinned_per_draw_layout.valid())
        {
            m_skinned_per_draw_layout = m_device->create_bind_group_layout(m_descriptor.skinned_draw_layout);
        }
        return m_skinned_per_draw_layout;
    }

    bool material_template::skins(uint32_t keywords) const
    {
        return (keywords & keyword_bit(material_keyword::skinned)) != 0 && !m_descriptor.skin_attributes.empty();
    }

    gpu::bind_group_layout material_template::per_material_layout() const
    {
        return m_per_material_layout;
    }

    bool material_template::has_frame_layout() const
    {
        return m_frame_layout.valid();
    }

    bool material_template::has_material_layout() const
    {
        return m_per_material_layout.valid();
    }

    uint32_t material_template::per_draw_slot() const
    {
        return has_frame_layout() ? 1u : 0u;
    }

    uint32_t material_template::per_material_slot() const
    {
        return per_draw_slot() + 1u;
    }

    assets::vertex_format material_template::required_vertex_format(uint32_t keywords) const
    {
        // A skinned record carries the tangent whether or not the variant
        // reads it, so the skin format stands for both.
        if (skins(keywords))
        {
            return m_descriptor.skinned_vertex_format;
        }
        return reads_tangents(keywords) ? m_descriptor.required_vertex_format
                                        : m_descriptor.vertex_format_without_tangents;
    }

    uint32_t material_template::min_vertex_stride(uint32_t keywords) const
    {
        const std::vector<gpu::vertex_buffer_layout> layouts = vertex_layouts_for(keywords);
        return layouts.empty() ? 0u : layout_extent(layouts.front());
    }

    const std::vector<material*>& material_template::instances() const
    {
        return m_instances;
    }

    void material_template::register_instance(material* instance)
    {
        m_instances.push_back(instance);
    }

    void material_template::unregister_instance(material* instance)
    {
        m_instances.erase(std::remove(m_instances.begin(), m_instances.end(), instance), m_instances.end());
    }

    const material_template::shader_set& material_template::shaders_for(uint32_t keywords)
    {
        // Only the keyword bits reach the preprocessor, so two keys that
        // differ in fixed-function state alone share one compile.
        if (const auto found = m_shaders.find(keywords); found != m_shaders.end())
        {
            return found->second;
        }

        shader_set shaders{};

        // Created through the library helper so a debug build's hot
        // reload can swap an edited stage in behind these handles and
        // rebuild every variant built from them in place.
        gpu::shader_variant vertex = m_descriptor.vertex_shader;
        vertex.defines = defines_for(keywords, m_descriptor.vertex_shader.defines);
        shaders.vertex = gpu::create_library_shader_module(*m_device, vertex, gpu::shader_stage::vertex);

        gpu::shader_variant fragment = m_descriptor.fragment_shader;
        fragment.defines = defines_for(keywords, m_descriptor.fragment_shader.defines);
        shaders.fragment = gpu::create_library_shader_module(*m_device, fragment, gpu::shader_stage::fragment);

        return m_shaders.emplace(keywords, shaders).first->second;
    }

    std::vector<gpu::vertex_buffer_layout> material_template::vertex_layouts_for(uint32_t keywords) const
    {
        std::vector<gpu::vertex_buffer_layout> layouts = m_descriptor.vertex_layouts;
        if (layouts.empty())
        {
            return layouts;
        }
        auto& attributes = layouts.front().attributes;
        if (!reads_tangents(keywords) &&
            m_descriptor.tangent_location != material_template_descriptor::no_tangent_location)
        {
            // A tangent-less variant must not declare the tangent
            // attribute: the record it draws is narrower, and the
            // attribute would fetch past every vertex.
            attributes.erase(std::remove_if(attributes.begin(),
                                            attributes.end(),
                                            [this](const gpu::vertex_attribute& attribute)
                                            { return attribute.location == m_descriptor.tangent_location; }),
                             attributes.end());
        }
        if (skins(keywords))
        {
            // The joints and weights sit after the tangent in the skinned
            // record, which keeps them in place whether or not the tangent
            // attribute itself is read.
            attributes.insert(
                attributes.end(), m_descriptor.skin_attributes.begin(), m_descriptor.skin_attributes.end());
        }
        return layouts;
    }

    gpu::shader_defines material_template::defines_for(uint32_t keywords, const gpu::shader_defines& base) const
    {
        // keyword_defines covers the engine keywords' bits; the template's
        // own follow, in declaration order.
        gpu::shader_defines defines = keyword_defines(keywords, base);
        for (std::size_t i = 0; i < m_descriptor.keywords.size(); ++i)
        {
            const uint32_t bit =
                1u << (material_template_descriptor::first_template_keyword + static_cast<uint32_t>(i));
            if ((keywords & bit) != 0)
            {
                defines.emplace_back(m_descriptor.keywords[i], "");
            }
        }
        return defines;
    }

    bool material_template::reads_tangents(uint32_t keywords) const
    {
        // A template without a tangent channel reads its full layout
        // regardless of the keyword; one with a channel reads it only
        // when the keyword asks for it.
        if (m_descriptor.tangent_location == material_template_descriptor::no_tangent_location)
        {
            return true;
        }
        return (keywords & keyword_bit(material_keyword::has_tangents)) != 0;
    }
} // namespace rendering_engine
