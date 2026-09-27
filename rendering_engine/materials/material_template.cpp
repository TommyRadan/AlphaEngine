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

#include <rendering_engine/materials/material_template.hpp>

#include <algorithm>
#include <utility>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>

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
} // namespace

namespace rendering_engine
{
    material_template::material_template(gpu::device& device, material_template_descriptor descriptor)
        : m_device(&device), m_descriptor(std::move(descriptor))
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
        // down (the renderer drops its materials, and with them the
        // templates they share, in context::quit), so the device is
        // live here.
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
        pipeline_descriptor.fragment_shader = shaders.fragment;
        pipeline_descriptor.vertex_buffers = vertex_layouts_for(key.keywords);
        pipeline_descriptor.topology = m_descriptor.topology;
        pipeline_descriptor.depth = to_depth_state(key);
        pipeline_descriptor.blend = to_blend_state(key.blending);
        pipeline_descriptor.rasterizer = to_rasterizer_state(key);
        if (m_descriptor.frame_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_descriptor.frame_layout);
        }
        pipeline_descriptor.bind_group_layouts.push_back(per_draw_layout(key.keywords));
        if (m_per_material_layout.valid())
        {
            pipeline_descriptor.bind_group_layouts.push_back(m_per_material_layout);
        }

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
        return m_descriptor.frame_layout.valid();
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

    vertex_format material_template::required_vertex_format(uint32_t keywords) const
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

        gpu::shader_variant vertex = m_descriptor.vertex_shader;
        vertex.defines = keyword_defines(keywords, m_descriptor.vertex_shader.defines);
        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_library_shader(vertex, gpu::shader_stage::vertex);
        shaders.vertex = m_device->create_shader_module(vs_descriptor);

        gpu::shader_variant fragment = m_descriptor.fragment_shader;
        fragment.defines = keyword_defines(keywords, m_descriptor.fragment_shader.defines);
        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_library_shader(fragment, gpu::shader_stage::fragment);
        shaders.fragment = m_device->create_shader_module(fs_descriptor);

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
